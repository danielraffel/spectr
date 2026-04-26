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

/// Convert a React DOM-style style object into @pulp/react style props.
/// CSS properties we know how to translate get hoisted; the rest are dropped.
function adaptStyle(style: CSSProperties | undefined): Record<string, unknown> {
    if (!style) return {};
    const out: Record<string, unknown> = {};
    if (style.background)        out.background       = style.background as string;
    if (style.backgroundColor)   out.background       = style.backgroundColor as string;
    if (style.color)             out.textColor        = style.color as string;
    if (style.opacity !== undefined)  out.opacity     = Number(style.opacity);
    if (style.width !== undefined)    out.width       = parseLen(style.width);
    if (style.height !== undefined)   out.height      = parseLen(style.height);
    if (style.minWidth !== undefined) out.minWidth    = parseLen(style.minWidth);
    if (style.minHeight !== undefined)out.minHeight   = parseLen(style.minHeight);
    if (style.maxWidth !== undefined) out.maxWidth    = parseLen(style.maxWidth);
    if (style.maxHeight !== undefined)out.maxHeight   = parseLen(style.maxHeight);
    if (style.padding !== undefined)  out.padding     = parseLen(style.padding);
    if (style.paddingLeft !== undefined)   out.paddingLeft   = parseLen(style.paddingLeft);
    if (style.paddingRight !== undefined)  out.paddingRight  = parseLen(style.paddingRight);
    if (style.paddingTop !== undefined)    out.paddingTop    = parseLen(style.paddingTop);
    if (style.paddingBottom !== undefined) out.paddingBottom = parseLen(style.paddingBottom);
    if (style.margin !== undefined)        out.margin        = parseLen(style.margin);
    if (style.gap !== undefined)           out.gap           = parseLen(style.gap);
    if (style.flexGrow !== undefined)      out.flexGrow      = Number(style.flexGrow);
    if (style.flexShrink !== undefined)    out.flexShrink    = Number(style.flexShrink);
    if (style.flexBasis !== undefined)     out.flexBasis     = parseLen(style.flexBasis);
    if (style.alignItems)                  out.alignItems    = String(style.alignItems);
    if (style.justifyContent)              out.justifyContent= String(style.justifyContent);
    if (style.flexDirection === 'row' || style.flexDirection === 'row-reverse') {
        out.direction = 'row';
    } else if (style.flexDirection === 'column' || style.flexDirection === 'column-reverse') {
        out.direction = 'column';
    }
    // The HTML editor uses `position: absolute, inset: 0` to mean
    // "fill the parent". On the bridge there's no real positioning
    // primitive yet, so we approximate that semantic by forcing the
    // container to grow + take the full available width. Without
    // this the entire app collapses to 0×0 because the outer
    // App-level div has no explicit dims.
    const pos = (style as { position?: string; inset?: number | string }).position;
    if (pos === 'absolute' || pos === 'fixed') {
        const inset = (style as { inset?: number | string }).inset;
        if (inset === 0 || inset === '0' || inset === '0px') {
            if (out.flexGrow === undefined) out.flexGrow = 1;
            // Window-fill heuristic: outer-most container at the App level.
            // pulp-screenshot uses 1320x860 by convention; the real plugin
            // editor is 1320x860 too. Hard-coding here is a v0.x band-aid
            // until <View positionFill> lands or we read host content size.
            if (out.width === undefined)  out.width  = 1320;
            if (out.height === undefined) out.height = 860;
        }
    }
    return out;
}

function parseLen(v: unknown): number | undefined {
    if (typeof v === 'number') return v;
    if (typeof v !== 'string') return undefined;
    if (v.endsWith('px')) return parseFloat(v);
    if (v === '0') return 0;
    if (v.endsWith('%')) return undefined; // bridge wants px; drop %
    return undefined;
}

/// Replacement for React.createElement that intercepts string tag names
/// and dispatches to @pulp/react components. Function components fall
/// through unchanged.
export function createElement(
    type: unknown,
    props: Record<string, unknown> | null,
    ...children: ReactNode[]
): ReactNode {
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
    if (typeof inProps.onClick === 'function') adapted.onClick = inProps.onClick;
    if (typeof inProps.onChange === 'function') adapted.onChange = inProps.onChange;
    if (typeof inProps.onMouseEnter === 'function') adapted.onMouseEnter = inProps.onMouseEnter;
    if (typeof inProps.onMouseLeave === 'function') adapted.onMouseLeave = inProps.onMouseLeave;
    if (inProps.ref !== undefined) (adapted as { ref?: unknown }).ref = inProps.ref;
    if (typeof inProps.value === 'string' || typeof inProps.value === 'number') {
        adapted.value = inProps.value;
    }

    // Drop className entirely — no global stylesheet on this lane.
    // (data-* attributes also get dropped.)

    return pulpCreateElement(target as never, adapted as never, ...children);
}

/// Re-exports so the extracted code can import them as if from React.
export { useState, useEffect, useRef, useCallback, useMemo, Fragment } from 'react';
