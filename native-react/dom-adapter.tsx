// dom-adapter.tsx — translates the extracted React app's DOM JSX
// (`<div>`, `<span>`, `<button>`, `<canvas>`, `<input>`, etc.) into
// @pulp/react bridge primitives. Designed to keep the extracted
// 3800-line spectr-editor-extracted.js close to verbatim — we feed
// our adapter as the React `createElement` so all JSX falls through
// here without per-component rewrites.
//
// Phase 1 scope: visible structure. CSS is parsed from inline `style`
// objects when present and forwarded as @pulp/react style props.
// className-based CSS is dropped (no global stylesheet on this lane).
// SVG fragments (svg/rect/path/circle/line) lower to <View> placeholders
// so the layout doesn't break — Phase 2 will redraw them via <Canvas>.

import {
    createElement as pulpCreateElement,
    type ReactNode, type CSSProperties,
} from 'react';
import {
    View, Row, Label, Button, Canvas, TextEditor,
} from '@pulp/react';
import { wrapCanvasInstance } from './canvas2d-shim.js';

// HTML tags we map to @pulp/react components. Anything not in this
// table falls through to View (so unknown tags render their children).
type Mapped = typeof View;
const TAG_MAP: Record<string, Mapped | null> = {
    div: View as Mapped,
    section: View as Mapped,
    main: View as Mapped,
    header: View as Mapped,
    footer: View as Mapped,
    nav: View as Mapped,
    aside: View as Mapped,
    article: View as Mapped,
    p: View as Mapped,
    span: Label as unknown as Mapped,
    h1: Label as unknown as Mapped,
    h2: Label as unknown as Mapped,
    h3: Label as unknown as Mapped,
    h4: Label as unknown as Mapped,
    h5: Label as unknown as Mapped,
    h6: Label as unknown as Mapped,
    label: Label as unknown as Mapped,
    button: Button as unknown as Mapped,
    canvas: Canvas as unknown as Mapped,
    input: TextEditor as unknown as Mapped,
    textarea: TextEditor as unknown as Mapped,
    // Phase-1 SVG elements: render as containers so layout stays valid.
    svg: View as Mapped,
    g: View as Mapped,
    rect: View as Mapped,
    line: View as Mapped,
    path: View as Mapped,
    circle: View as Mapped,
    text: Label as unknown as Mapped,
};

/// Table-driven CSS-key → bridge-prop translation. Each row is
/// {cssKey, hostKey, parser}. Lets us stay declarative and extend
/// in one place rather than scattering switch statements.
const STYLE_MAP: Array<{
    css: string;
    host: string;
    parse?: (v: unknown) => unknown;
}> = [
    // Visual
    { css: 'background',         host: 'background' },
    { css: 'backgroundColor',    host: 'background' },
    { css: 'backgroundImage',    host: 'background' },
    { css: 'color',              host: 'textColor' },
    { css: 'opacity',            host: 'opacity', parse: Number },
    // Box
    { css: 'width',              host: 'width', parse: parseLen },
    { css: 'height',             host: 'height', parse: parseLen },
    { css: 'minWidth',           host: 'minWidth', parse: parseLen },
    { css: 'minHeight',          host: 'minHeight', parse: parseLen },
    { css: 'maxWidth',           host: 'maxWidth', parse: parseLen },
    { css: 'maxHeight',          host: 'maxHeight', parse: parseLen },
    // Padding
    { css: 'padding',            host: 'padding', parse: parseLen },
    { css: 'paddingLeft',        host: 'paddingLeft', parse: parseLen },
    { css: 'paddingRight',       host: 'paddingRight', parse: parseLen },
    { css: 'paddingTop',         host: 'paddingTop', parse: parseLen },
    { css: 'paddingBottom',      host: 'paddingBottom', parse: parseLen },
    // Margin (full set — was missing marginLeft/Right per dropped-style telemetry)
    { css: 'margin',             host: 'margin', parse: parseLen },
    { css: 'marginLeft',         host: 'marginLeft', parse: parseLen },
    { css: 'marginRight',        host: 'marginRight', parse: parseLen },
    { css: 'marginTop',          host: 'marginTop', parse: parseLen },
    { css: 'marginBottom',       host: 'marginBottom', parse: parseLen },
    // Flex
    { css: 'gap',                host: 'gap', parse: parseLen },
    { css: 'rowGap',             host: 'rowGap', parse: parseLen },
    { css: 'columnGap',          host: 'columnGap', parse: parseLen },
    { css: 'flexGrow',           host: 'flexGrow', parse: Number },
    { css: 'flexShrink',         host: 'flexShrink', parse: Number },
    { css: 'flexBasis',          host: 'flexBasis', parse: parseLen },
    { css: 'alignItems',         host: 'alignItems', parse: String },
    { css: 'alignSelf',          host: 'alignSelf', parse: String },
    { css: 'alignContent',       host: 'alignContent', parse: String },
    { css: 'justifyContent',     host: 'justifyContent', parse: String },
    { css: 'flexWrap',           host: 'flexWrap', parse: String },
    // Borders
    { css: 'borderRadius',       host: 'borderRadius', parse: parseLen },
    // Text
    { css: 'fontSize',           host: 'fontSize', parse: parseLen },
    { css: 'fontFamily',         host: 'fontFamily', parse: String },
    { css: 'fontWeight',         host: 'fontWeight', parse: (v) => typeof v === 'number' ? v : String(v) },
    { css: 'letterSpacing',      host: 'letterSpacing', parse: parseLen },
    { css: 'lineHeight',         host: 'lineHeight', parse: parseLen },
    { css: 'textAlign',          host: 'textAlign', parse: String },
];

function adaptStyle(style: CSSProperties | undefined): Record<string, unknown> {
    if (!style) return {};
    const out: Record<string, unknown> = {};
    const styleObj = style as Record<string, unknown>;

    // Walk the table — CSS keys we know about.
    for (const { css, host, parse } of STYLE_MAP) {
        const v = styleObj[css];
        if (v === undefined || v === null) continue;
        const p = parse ? parse(v) : v;
        if (p !== undefined) out[host] = p;
    }

    // Background gradient → first-color-stop fallback. The bridge's
    // setBackground claims to parse linear-gradient strings but in
    // practice (verified spectr#28 v0.48.0) the toolbar/footer end up
    // rendered with the default View background (white). Until the
    // upstream parser is verified end-to-end, extract the first
    // recognizable color from the gradient and use that as a solid
    // background. Picks the dominant top-of-gradient color so dark
    // surfaces stay dark and light text is readable.
    const bg = out.background;
    if (typeof bg === 'string' && bg.includes('gradient(')) {
        // Find the first rgba(...) / rgb(...) / #hex / oklch(...) inside.
        const m =
            bg.match(/(rgba?\([^)]+\))/i) ||
            bg.match(/(#[0-9a-f]{3,8})/i) ||
            bg.match(/(oklch\([^)]+\))/i);
        if (m) {
            out.background = m[1];
        } else {
            // Truly unparseable — drop the gradient entirely (don't pass
            // a string the bridge will silently reject).
            delete out.background;
        }
    }

    // flexDirection: collapse row-reverse/column-reverse to row/column.
    // In HTML/CSS, `display: flex` defaults to `flex-direction: row`. Pulp's
    // createCol creates a column container by default, so `<div style={{
    // display: 'flex' }}>` (without an explicit flexDirection) ends up as
    // a column when it should be a row — children stack vertically and
    // overflow the parent's height. Bridge to HTML semantics: when display
    // is flex without an explicit direction, default to row.
    if (style.flexDirection === 'row' || style.flexDirection === 'row-reverse') {
        out.direction = 'row';
    } else if (style.flexDirection === 'column' || style.flexDirection === 'column-reverse') {
        out.direction = 'column';
    } else if (style.display === 'flex' || style.display === 'inline-flex') {
        out.direction = 'row';
    }

    // border shorthand → setBorder({color, width}). CSS form:
    // "1px solid rgba(...)" / "1px solid #fff"
    const parseBorder = (val: unknown): { color: string; width: number } | undefined => {
        if (typeof val !== 'string') return undefined;
        const m = val.match(/^\s*(\d+(?:\.\d+)?)px\s+\w+\s+(.+)$/);
        if (!m) return undefined;
        return { width: parseFloat(m[1]!), color: m[2]!.trim() };
    };
    if (styleObj.border !== undefined) {
        const b = parseBorder(styleObj.border);
        if (b) out.border = b;
    }
    if (styleObj.borderTop !== undefined) {
        const b = parseBorder(styleObj.borderTop);
        if (b) out.borderTop = b;
    }
    if (styleObj.borderRight !== undefined) {
        const b = parseBorder(styleObj.borderRight);
        if (b) out.borderRight = b;
    }
    if (styleObj.borderBottom !== undefined) {
        const b = parseBorder(styleObj.borderBottom);
        if (b) out.borderBottom = b;
    }
    if (styleObj.borderLeft !== undefined) {
        const b = parseBorder(styleObj.borderLeft);
        if (b) out.borderLeft = b;
    }

    // display: 'none' → setVisible(false). 'flex'/'block' default.
    if (styleObj.display === 'none') {
        out.visible = false;
    }

    // overflow: 'hidden' → bridge clip. (No bridge support yet; warn-only.)
    if (styleObj.overflow === 'hidden') {
        // Drop silently — known unsupported. Bridge needs setClip.
    }

    // pointerEvents: 'none' → bridge equivalent (?). Drop for now;
    // overlays with pointerEvents:none should pass clicks through.
    if (styleObj.pointerEvents === 'none') {
        // setHitTest equivalent not in bridge yet; track separately.
        out.__pointerEventsNone = true;
    }

    // transform: parse translateX(N%)/translateY(N%) so common
    // centering idiom (left:50% + transform:translateX(-50%)) works.
    // General CSS transform unsupported.
    if (typeof styleObj.transform === 'string') {
        const tx = styleObj.transform.match(/translateX\(([-\d.]+)%\)/);
        const ty = styleObj.transform.match(/translateY\(([-\d.]+)%\)/);
        if (tx) out.__translateXPercent = parseFloat(tx[1]!);
        if (ty) out.__translateYPercent = parseFloat(ty[1]!);
    }
    // CSS positioning. Pulp exposes setPosition + setTop/Left/Right/Bottom.
    // The HTML editor uses `position: absolute, inset: 0` as a "fill
    // parent" idiom for stacked overlays (header + FilterBank + Chrome
    // all want to occupy the App's container, layered). Converting to
    // flex-grow:1 broke that — siblings competed for the same axis
    // space, only the first survived. The right translation is Yoga's
    // absolute-with-4-zero-edges, which gives "fill parent" via
    // positioning, leaving each sibling fully sized at (0,0).
    const styleAny = style as Record<string, unknown>;
    const pos = styleAny.position as string | undefined;
    const inset = styleAny.inset;
    const isInsetZero = inset === 0 || inset === '0' || inset === '0px';
    if (pos === 'absolute' || pos === 'fixed') {
        out.position = 'absolute';
        if (isInsetZero) {
            // Yoga: absolute + 4 zero edges = fill parent. No width/height.
            out.top = 0; out.left = 0; out.right = 0; out.bottom = 0;
        }
    } else if (pos) {
        out.position = pos;
    }
    // Explicit edge overrides (after inset-fill so they can override)
    if (styleAny.top !== undefined)    out.top    = parseLen(styleAny.top);
    if (styleAny.left !== undefined)   out.left   = parseLen(styleAny.left);
    if (styleAny.right !== undefined)  out.right  = parseLen(styleAny.right);
    if (styleAny.bottom !== undefined) out.bottom = parseLen(styleAny.bottom);
    // CSS z-index → bridge setZIndex (Pulp supports it; needed for
    // overlays/menus per spectr#28 codex review).
    if (styleAny.zIndex !== undefined) out.zIndex = Number(styleAny.zIndex);

    // Telemetry — log any style key we DIDN'T translate. Once-per-key.
    // Lets us see what the editor wants but we're silently dropping
    // (the next-iteration prioritization signal). Enumerated keys above
    // count as "handled" for this set.
    const HANDLED = new Set<string>([
        'background', 'backgroundColor', 'backgroundImage', 'color', 'opacity',
        'width', 'height', 'minWidth', 'minHeight', 'maxWidth', 'maxHeight',
        'padding', 'paddingLeft', 'paddingRight', 'paddingTop', 'paddingBottom',
        'margin', 'marginLeft', 'marginRight', 'marginTop', 'marginBottom',
        'gap', 'rowGap', 'columnGap',
        'flexGrow', 'flexShrink', 'flexBasis', 'flexWrap', 'flex',
        'alignItems', 'alignSelf', 'alignContent', 'justifyContent', 'flexDirection',
        'position', 'inset', 'top', 'left', 'right', 'bottom', 'zIndex',
        'border', 'borderTop', 'borderRight', 'borderBottom', 'borderLeft', 'borderRadius',
        'fontSize', 'fontFamily', 'fontWeight', 'letterSpacing', 'lineHeight', 'textAlign',
        'display', 'overflow', 'pointerEvents', 'transform',
        // Drop-quietly (known visual polish, no bridge support yet)
        'cursor', 'touchAction', 'transition', 'boxShadow', 'backdropFilter',
        'accentColor', 'verticalAlign', 'whiteSpace', 'textOverflow', 'userSelect',
    ]);
    for (const key of Object.keys(styleAny)) {
        if (!HANDLED.has(key)) warnDropped(key);
    }
    return out;
}

function parseLen(v: unknown): number | string | undefined {
    if (typeof v === 'number') return v;
    if (typeof v !== 'string') return undefined;
    if (v.endsWith('px')) return parseFloat(v);
    if (v === '0') return 0;
    // Yoga supports percent on positions/dimensions — pass through as
    // string so prop-applier / bridge can recognize the unit.
    if (v.endsWith('%')) return v;
    // Targeted calc(100% + Npx) — common centering offset; resolve to
    // a percent + px pair we forward as-is. General calc() is unsupported.
    const calcM = v.match(/^calc\(\s*100%\s*([+-])\s*(\d+(?:\.\d+)?)px\s*\)$/);
    if (calcM) {
        // For now drop and warn; the bridge can't represent compound calc.
        warnDropped('calc:' + v);
        return undefined;
    }
    if (v.endsWith('em') || v.endsWith('rem')) return undefined;  // unsupported
    return undefined;
}

// Once-per-prop telemetry for dropped style keys. Lets us see what the
// editor wants but we're silently ignoring — major signal for next-fix
// prioritization. Logs through __spectrLog (NativeEditorView, stderr).
const _droppedSeen = new Set<string>();
function warnDropped(key: string): void {
    if (_droppedSeen.has(key)) return;
    _droppedSeen.add(key);
    const lg = (globalThis as { __spectrLog?: (...a: unknown[]) => void }).__spectrLog;
    if (lg) lg('[adapter:dropped-style] ' + key);
}

/// Replacement for React.createElement that intercepts string tag names
/// and dispatches to @pulp/react components. Function components fall
/// through unchanged.
let __ce_count = 0;
export function createElement(
    type: unknown,
    props: Record<string, unknown> | null,
    ...children: ReactNode[]
): ReactNode {
    __ce_count++;
    if (__ce_count <= 12) {
        const desc = typeof type === 'string' ? type
            : typeof type === 'function' ? '<' + ((type as { name?: string }).name || 'fn') + '>'
            : String(type);
        const log = (globalThis as { console?: { log?: (...a: unknown[]) => void } }).console?.log;
        if (log) log('[adapter] #' + __ce_count + ' ce(' + desc + ')  children=' + children.length);
    }
    if (typeof type !== 'string') {
        // Function/class component — pass through.
        return pulpCreateElement(type as never, props as never, ...children);
    }
    const tag = type.toLowerCase();
    const target = TAG_MAP[tag] ?? View as Mapped;

    const inProps = (props ?? {}) as Record<string, unknown>;
    const styleObj = inProps.style as CSSProperties | undefined;
    const adapted: Record<string, unknown> = { ...adaptStyle(styleObj) };

    // Forward common DOM attrs to bridge props
    if (typeof inProps.id === 'string') adapted.id = inProps.id;
    // Event handler forwarding. Pulp's bridge `on(id, eventName, fn)`
    // routes events to JS, so any DOM-style on* handler the editor
    // attaches gets forwarded with its eventName lowercased. This
    // supports onClick / onChange / onPointerDown / onPointerMove /
    // onPointerUp / onPointerLeave / onWheel / onContextMenu — all of
    // which the FilterBank/Chrome use for canvas editing.
    for (const key of Object.keys(inProps)) {
        if (key.length > 2 && key.startsWith('on') && key[2] !== undefined && key[2] === key[2].toUpperCase()) {
            const v = inProps[key];
            if (typeof v === 'function') adapted[key] = v;
        }
    }
    // ref handling: for <canvas>, wrap the underlying instance with a
    // Canvas2D-compatible shim before forwarding to the caller's ref so
    // their `canvasRef.current.getContext('2d').fillRect(...)` works.
    if (inProps.ref !== undefined) {
        const userRef = inProps.ref as
            | ((v: unknown) => void)
            | { current: unknown };
        if (tag === 'canvas') {
            const callback = (instance: unknown) => {
                const wrapped = instance && (instance as { id?: string }).id
                    ? wrapCanvasInstance(instance as { id: string })
                    : instance;
                if (typeof userRef === 'function') userRef(wrapped);
                else if (userRef) (userRef as { current: unknown }).current = wrapped;
            };
            (adapted as { ref?: unknown }).ref = callback;
        } else {
            (adapted as { ref?: unknown }).ref = inProps.ref;
        }
    }
    if (typeof inProps.value === 'string' || typeof inProps.value === 'number') {
        adapted.value = inProps.value;
    }

    // Drop className entirely — no global stylesheet on this lane.
    // (data-* attributes also get dropped.)

    return pulpCreateElement(target as never, adapted as never, ...children);
}

/// Re-exports so the extracted code can import them as if from React.
export { useState, useEffect, useRef, useCallback, useMemo, Fragment } from 'react';
