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

// HTML tags we map to @pulp/react bridge type strings. Using strings
// directly (rather than the @pulp/react React-component wrappers)
// because the wrappers are plain function components without
// forwardRef — React silently drops ref props passed to them, so
// `<div ref={wrapRef}>` never fires the ref callback. With raw
// strings, React's reconciler routes refs through getPublicInstance.
type Mapped = string;
const TAG_MAP: Record<string, Mapped | null> = {
    div: 'View',
    section: 'View',
    main: 'View',
    header: 'View',
    footer: 'View',
    nav: 'View',
    aside: 'View',
    article: 'View',
    p: 'View',
    span: 'Label',
    h1: 'Label',
    h2: 'Label',
    h3: 'Label',
    h4: 'Label',
    h5: 'Label',
    h6: 'Label',
    label: 'Label',
    // Map <button> to View so mixed children (svg + span etc.) render
    // as the button's contents. @pulp/react's Button widget collapses
    // multi-child to its first text via asText() which returns '' for
    // mixed arrays, leaving the visual button label empty. Click
    // handlers still route via dom-adapter's on* forwarding regardless
    // of widget type, so View-as-button doesn't lose interactivity.
    button: 'View',
    canvas: 'Canvas',
    input: 'TextEditor',
    textarea: 'TextEditor',
    // Phase-1 SVG elements: render as containers so layout stays valid.
    svg: 'View',
    g: 'View',
    rect: 'View',
    line: 'View',
    // <path> prefers the @pulp/react SvgPath intrinsic (pulp #994 /
    // #1291, shipped in v0.69.2). Detected at runtime — falls back to
    // 'View' + ref-callback (`_buildSvgPathRef`) on older SDKs that
    // don't expose the bridge primitive. The actual selection happens
    // in `tagToWidget` below since we need to inspect globalThis.
    path: 'View',
    circle: 'View',
    text: 'Label',
};

/// Returns the host-element type for a given HTML tag, with runtime
/// upgrade for `<path>` to the SvgPath intrinsic when @pulp/react +
/// the bridge support it (v0.69.2+). Falls back to the static map for
/// every other tag.
function tagToWidget(tag: string): string {
    if (tag === 'path') {
        const g = globalThis as Record<string, unknown>;
        // SvgPath JSX intrinsic landed via #1291 — bridge fns
        // createSvgPath/setSvgPath/setSvgViewBox/setSvgFill/
        // setSvgStroke/setSvgStrokeWidth all need to be registered.
        // When all present, render as the intrinsic and let prop-applier
        // wire `d` / `viewBox` / `fill` / `stroke` / `strokeWidth`.
        // The legacy ref-callback workaround stays as a fallback for
        // pre-v0.69.2 SDKs (still fires below in the SvgPath code path).
        if (typeof g.createSvgPath === 'function' &&
            typeof g.setSvgPath === 'function') {
            return 'SvgPath';
        }
    }
    return TAG_MAP[tag] ?? 'View';
}

/// Spectr #32 — pulp gates pointer dispatch behind registerPointer(id).
/// @pulp/react's prop-applier only wires registerHover, so onPointerDown
/// listeners install in the JS dispatch table but never fire. We arm
/// pointer dispatch from the dom-adapter when an instance is mounted —
/// but ONLY ONCE per id. Calling registerPointer on every ref-mount
/// re-creates the bridge lambda each time and starves the canvas paint
/// pump (manifests as a blank canvas with constantly-flickering
/// unmount-remount cycles).
const __pointerArmed: Set<string> = new Set();
function armPointerOnce(id: string): void {
    if (__pointerArmed.has(id)) return;
    const rp = (globalThis as { registerPointer?: (s: string) => void }).registerPointer;
    if (typeof rp === 'function') {
        try { rp(id); __pointerArmed.add(id); } catch (_e) {}
    }
}

/// Spectr #32 — flash-on-hover root cause: dom-adapter is invoked on
/// every render, and previously created a NEW ref callback function
/// each call. React's reconciler sees a different ref function and
/// detaches (calls old with null) + re-attaches (calls new with the
/// instance) — which the dom-adapter logs as `ref-cb` null/object
/// pairs. Each detach/re-attach churns the bridge's widget bookkeeping
/// and starves the rAF paint pump → canvas flashes blank during any
/// re-render-triggering event (mouse move via setHover, etc.).
///
/// Fix: cache the ref callback per (userRef × tag) so we hand React
/// the SAME function identity across renders. Use a WeakMap keyed on
/// the user's ref so the cache entry is collected when the user's
/// component unmounts.
const __canvasRefCache: WeakMap<object, (inst: unknown) => void> = new WeakMap();
const __nonCanvasRefCache: WeakMap<object, (inst: unknown) => void> = new WeakMap();
function refCacheKey(userRef: unknown): object | null {
    if (typeof userRef === 'function') return userRef as unknown as object;
    if (userRef && typeof userRef === 'object') return userRef as object;
    return null;
}

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
    { css: 'fontFamily',         host: 'fontFamily', parse: (v) => resolveTokens(String(v)) },
    { css: 'fontWeight',         host: 'fontWeight', parse: (v) => typeof v === 'number' ? v : String(v) },
    { css: 'letterSpacing',      host: 'letterSpacing', parse: parseLen },
    { css: 'lineHeight',         host: 'lineHeight', parse: parseLen },
    { css: 'textAlign',          host: 'textAlign', parse: String },
];

// Design tokens auto-extracted from the original Spectr-standalone.html
// template's :root and .scheme-* blocks. Generated by:
//   node tools/extract-html-bundle/extract.mjs <html> <out-dir>
// 25 default tokens × 4 themes (default/paper/dusk/terminal). For now we
// always resolve against `default`. Theme switching = rebuild — runtime
// CSS-variable cascading isn't supported by the bridge yet.
import tokensRaw from './extracted/tokens.json' with { type: 'json' };
import classNamesRaw from './extracted/classnames.json' with { type: 'json' };
const TOKENS = (tokensRaw as Record<string, Record<string, string>>).default;
const CLASS_STYLES = classNamesRaw as Record<string, Record<string, unknown>>;

/// Replace var(--name) and var(--name, fallback) substrings inline.
/// Idempotent — non-var strings pass through unchanged.
function resolveTokens(value: string): string {
    if (!value.includes('var(')) return value;
    return value.replace(/var\((--[a-zA-Z0-9_-]+)(?:\s*,\s*([^)]+))?\)/g,
        (_match, name: string, fallback?: string) => {
            const v = TOKENS[name];
            if (v !== undefined) return v;
            return (fallback ?? '').trim() || _match;
        });
}

/// Resolve className="foo bar" into a merged style object using the
/// extracted CSS class rules. Multiple classes merge left-to-right
/// (later wins, matching CSS rule application order). User's inline
/// style takes final precedence — applied on top by the caller.
function classNameToStyle(className: string | undefined): Record<string, unknown> {
    if (!className || typeof className !== 'string') return {};
    const out: Record<string, unknown> = {};
    for (const cls of className.trim().split(/\s+/)) {
        const rule = CLASS_STYLES[cls];
        if (rule) Object.assign(out, rule);
    }
    return out;
}

// Expand CSS shorthand for padding/margin: "2px 7px" → {top:2,right:7,bottom:2,left:7}.
// Returns {top, right, bottom, left} numbers, or null if the value is a single
// scalar that should be set on the shorthand prop directly.
function expandBoxShorthand(v: unknown):
    | { top: number; right: number; bottom: number; left: number }
    | null {
    if (typeof v !== 'string') return null;
    const tokens = v.trim().split(/\s+/).map(t => {
        if (t.endsWith('px')) return parseFloat(t);
        if (t === '0') return 0;
        const n = Number(t);
        return Number.isFinite(n) ? n : NaN;
    });
    if (tokens.length < 2 || tokens.some(t => !Number.isFinite(t))) return null;
    const [t, r = t, b = t, l = r] = tokens;
    return { top: t!, right: r!, bottom: b!, left: l! };
}

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

    // Padding/margin shorthand expansion. The bundle uses "2px 7px" form
    // heavily (button padding, panel insets); without expansion these
    // drop, and buttons collapse to text-height with no inset.
    for (const k of ['padding', 'margin'] as const) {
        const v = styleObj[k];
        if (typeof v === 'string' && v.includes(' ')) {
            const box = expandBoxShorthand(v);
            if (box) {
                out[k + 'Top'] = box.top;
                out[k + 'Right'] = box.right;
                out[k + 'Bottom'] = box.bottom;
                out[k + 'Left'] = box.left;
                delete out[k];
            }
        }
    }

    // CSS `flex` shorthand. Most-common forms in the bundle:
    //   flex: 1         → grow:1, shrink:1, basis:0%
    //   flex: 1 1 auto  → grow:1, shrink:1, basis:auto
    //   flex: 0 0 200px → grow:0, shrink:0, basis:200px
    // Spacers like `<div style={{ flex: 1 }} />` are crucial — without
    // this expansion the chrome's spacer collapses and right-aligned
    // toolbar groups overlap with text on the left.
    const flexV = styleObj.flex;
    if (flexV !== undefined && out.flexGrow === undefined) {
        if (typeof flexV === 'number') {
            out.flexGrow = flexV;
            out.flexShrink = 1;
            out.flexBasis = 0;
        } else if (typeof flexV === 'string') {
            const parts = flexV.trim().split(/\s+/);
            if (parts.length === 1) {
                const n = Number(parts[0]);
                if (Number.isFinite(n)) {
                    out.flexGrow = n;
                    out.flexShrink = 1;
                    out.flexBasis = 0;
                }
            } else if (parts.length === 3) {
                const g = Number(parts[0]);
                const s = Number(parts[1]);
                const b = parseLen(parts[2]!);
                if (Number.isFinite(g)) out.flexGrow = g;
                if (Number.isFinite(s)) out.flexShrink = s;
                if (b !== undefined) out.flexBasis = b;
            }
        }
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

    // overflow: 'hidden' is silently dropped (Pulp default). 'visible' is
    // routed via a ref-callback in createElement (see _buildOverflowRef);
    // the prop-applier in @pulp/react doesn't have a case for overflow.

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
            // Yoga: absolute + 4 zero edges = fill parent.
            out.top = 0; out.left = 0; out.right = 0; out.bottom = 0;
            if (out.width === undefined && out.height === undefined) {
                out.width = 1320;
                out.height = 860;
            }
        }
        // Overlay click-routing opt-in (pulp #1148 / #1297). Any
        // position:absolute element that ISN'T fill-parent (so
        // pop-overs / dropdowns / modals — not 4-edge-pinned overlays
        // like the FilterBank canvas backplate) is a candidate for
        // first-crack click routing via View::active_overlay_. Activates
        // when @pulp/react has the `overlay` prop AND the bridge has
        // claimOverlay/releaseOverlay registered (v0.71.0+ via #1297);
        // no-op pre-v0.71.0.
        if (!isInsetZero) {
            const g = globalThis as Record<string, unknown>;
            if (typeof g.claimOverlay === 'function') {
                out.overlay = true;
            }
        }
    } else if (pos) {
        out.position = pos;
    }
    // Pulp's Yoga doesn't reliably compute width/height from 3-edge
    // absolute pinning (e.g. top+left+right with explicit height) — leaves
    // the element with 0 measured width, which collapses every Label
    // descendant. Mimic CSS by injecting an explicit dimension when a
    // 3-edge pin defines a stretch axis.
    const hasTop = (styleAny.top !== undefined) || (pos === 'absolute' && isInsetZero);
    const hasLeft = (styleAny.left !== undefined) || (pos === 'absolute' && isInsetZero);
    const hasRight = (styleAny.right !== undefined) || (pos === 'absolute' && isInsetZero);
    const hasBottom = (styleAny.bottom !== undefined) || (pos === 'absolute' && isInsetZero);
    if (out.position === 'absolute') {
        // Stretch-X: top + left + right pinned, height explicit, no width
        if (hasLeft && hasRight && out.width === undefined) {
            out.width = 1320;
        }
        // Stretch-Y: top + bottom pinned, width explicit, no height
        if (hasTop && hasBottom && out.height === undefined) {
            out.height = 860;
        }
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

/// Workaround for pulp #972 (View::paint_all() doesn't honor z_index()).
/// Today, paint order = child insertion order. Stable-sort children by
/// their style.zIndex ascending so higher-zIndex elements paint LAST and
/// appear on top. Children without zIndex stay in original order
/// (default zIndex=0). This makes dropdowns/popovers (zIndex=20+) elevate
/// over their siblings even though the framework ignores setZIndex().
///
/// Safe because:
/// - Stable sort: equal-zIndex siblings keep insertion order
/// - In modern JS engines (QuickJS, V8) Array.prototype.sort is stable
/// - Most children have zIndex=undefined → 0 → no movement
/// - Only the few popovers with explicit zIndex move to end, preserving
///   flex layout for the normal-flow majority
/// Workaround for pulp #994 (`@pulp/react` doesn't have an `SvgPath` intrinsic
/// yet). Once #991 lands the C++ widget + JS bridge in v0.61.0, we route inline
/// `<svg><path d="...">` icon JSX to the new bridge functions via a ref
/// callback — same pattern as the overflow workaround. Activates only when
/// `globalThis.createSvgPath` exists (v0.61.0+); otherwise no-op.
function _buildSvgPathRef(
    pathD: string,
    fill: string | undefined,
    stroke: string | undefined,
    strokeWidth: number | undefined,
    viewBoxW: number | undefined,
    viewBoxH: number | undefined,
    existingRef: unknown,
): (instance: unknown) => void {
    return (instance: unknown) => {
        if (instance && typeof instance === 'object') {
            const id = (instance as { id?: unknown }).id;
            const parentId = (instance as { parent?: { id?: unknown } }).parent?.id;
            const g = globalThis as Record<string, unknown>;
            if (typeof id === 'string' && typeof g.createSvgPath === 'function') {
                try {
                    // The widget is already mounted as a View placeholder.
                    // Upgrade in-place: create the SvgPath beside it OR
                    // reconfigure via setSvgPath. The framework PR #991 likely
                    // requires a fresh widget — for now, set the path on the
                    // current id and rely on host-config future support.
                    (g.setSvgPath as (id: string, d: string) => unknown)(id, pathD);
                    if (viewBoxW !== undefined && viewBoxH !== undefined && typeof g.setSvgViewBox === 'function') {
                        (g.setSvgViewBox as (id: string, w: number, h: number) => unknown)(id, viewBoxW, viewBoxH);
                    }
                    if (fill !== undefined && typeof g.setSvgFill === 'function') {
                        (g.setSvgFill as (id: string, c: string) => unknown)(id, fill);
                    }
                    if (stroke !== undefined && typeof g.setSvgStroke === 'function') {
                        (g.setSvgStroke as (id: string, c: string) => unknown)(id, stroke);
                    }
                    if (strokeWidth !== undefined && typeof g.setSvgStrokeWidth === 'function') {
                        (g.setSvgStrokeWidth as (id: string, n: number) => unknown)(id, strokeWidth);
                    }
                    void parentId; // reserved for future createSvgPath(id, parentId) re-creation path
                } catch { /* swallow — pre-v0.61.0 bridges silently no-op */ }
            }
        }
        if (typeof existingRef === 'function') (existingRef as (i: unknown) => void)(instance);
        else if (existingRef && typeof existingRef === 'object') {
            (existingRef as { current: unknown }).current = instance;
        }
    };
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
    let target = tagToWidget(tag);

    // <span> structural-vs-leaf disambiguation. The static map sends every
    // <span> to Label, but React UIs use <span> as a styled inline-block
    // container at least as often as a text leaf — e.g. dropdown rows in
    // EditModePopover wrap their label/description in `<span style={{ flex: 1 }}>
    //   <span>{m.label}</span><span>{m.desc}</span></span>`. Mapping the
    // outer span to Label produces a Label widget whose children are two
    // element nodes; @pulp/react's asText() returns '' for that, the Label
    // ends up empty, and the row paints as a highlighted-but-blank rectangle.
    // Promote span (and h1-h6/label) to View whenever its children are
    // anything other than pure string/number — the wrap-string-as-Label
    // path below then synthesizes Labels for any direct text children with
    // proper textColor/font inheritance. Leaf <span>{text}</span> stays
    // mapped to Label so existing minWidth + multi-fragment concatenation
    // paths keep working.
    if (target === 'Label' && tag !== 'text') {
        let hasElementChild = false;
        for (const c of children) {
            if (c == null || c === false || c === true) continue;
            if (typeof c === 'string' || typeof c === 'number') continue;
            hasElementChild = true;
            break;
        }
        if (!hasElementChild) {
            const ic = (props as { children?: unknown } | null)?.children;
            if (Array.isArray(ic)) {
                for (const c of ic) {
                    if (c == null || c === false || c === true) continue;
                    if (typeof c === 'string' || typeof c === 'number') continue;
                    hasElementChild = true;
                    break;
                }
            } else if (ic != null && typeof ic !== 'string' && typeof ic !== 'number'
                       && ic !== false && ic !== true) {
                hasElementChild = true;
            }
        }
        if (hasElementChild) target = 'View';
    }

    const inProps = (props ?? {}) as Record<string, unknown>;
    // <input type="range"> → Fader (Pulp's linear control). Plain HTML
    // ranges have no DSP-style decoration; Fader handles the geometry +
    // drag interaction we need until pulp#966 lands a dedicated range
    // slider widget. Default orientation horizontal — matches the
    // overwhelming majority of <input type=range> usage in the wild.
    if (tag === 'input' && (inProps.type as string | undefined) === 'range') {
        target = 'Fader';
    }
    // className → style merge. CSS class rules from the original HTML's
    // <style> blocks are pre-flattened (see extracted/classnames.json);
    // merge them under the user's inline style so inline still wins.
    const classStyle = classNameToStyle(inProps.className as string | undefined);
    const inlineStyle = (inProps.style ?? {}) as Record<string, unknown>;
    const styleObj = { ...classStyle, ...inlineStyle } as CSSProperties;
    const adapted: Record<string, unknown> = { ...adaptStyle(styleObj) };

    // Workaround for Pulp Label measure-callback returning 0 width in
    // nested flex containers without explicit parent width: estimate
    // text width via charlen × fontSize and force minWidth so Yoga
    // can't collapse the Label below readable size. Stop-gap until
    // upstream measure-callback fix lands (currently #945's actual
    // root cause is in this layer, not Pulp's framework — see G3-G7
    // gate tests).
    // Default flexShrink to 0 globally so non-spacer items don't shrink
    // below their content. Spacers in the bundle use `flex: 1` which
    // sets flexGrow=1 + flexShrink=1 explicitly — they remain shrinkable.
    if (adapted.flexShrink === undefined && adapted.flexGrow === undefined) {
        adapted.flexShrink = 0;
    }

    // <button>-as-View defaults to Yoga's column direction, but browsers
    // flow inline-button children horizontally. Without this default,
    // an svg-glyph + text span inside a button stack vertically, busting
    // the toolbar row height (26px) — bottom-toolbar items disappear
    // because content overflows in y. Mirror browser inline-flex.
    if (tag === 'button' && adapted.direction === undefined) {
        adapted.direction = 'row';
        if (adapted.alignItems === undefined) adapted.alignItems = 'center';
    }

    // <input type="range"> → Fader: forward min/max/step/value as direct
    // bridge props, default horizontal orientation, ensure a reasonable
    // height (Fader's default vertical assumption gives 0 height in a row
    // flex parent without it).
    if (tag === 'input' && (inProps.type as string | undefined) === 'range') {
        adapted.orientation = adapted.orientation ?? 'horizontal';
        if (adapted.height === undefined) adapted.height = 12;
        if (adapted.flexShrink === undefined) adapted.flexShrink = 0;
        const minP = (inProps as { min?: unknown }).min;
        const maxP = (inProps as { max?: unknown }).max;
        const stepP = (inProps as { step?: unknown }).step;
        const valueP = (inProps as { value?: unknown }).value;
        if (minP !== undefined) adapted.min = Number(minP);
        if (maxP !== undefined) adapted.max = Number(maxP);
        if (stepP !== undefined) adapted.step = Number(stepP);
        if (valueP !== undefined) adapted.value = Number(valueP);
    }

    // SVG / IMG: read width/height as HTML attributes too (not just style).
    // <path d="..." stroke fill strokeWidth>: route to SvgPath bridge functions
    // via a ref-callback. Workaround for pulp #994 — @pulp/react doesn't have
    // an SvgPath intrinsic yet, so we call the v0.61.0 bridge functions
    // directly. Activates only when the bridge has the functions registered;
    // otherwise no-op (pre-v0.61.0 SDK).
    if (tag === 'path') {
        const d = (inProps as { d?: unknown }).d;
        if (typeof d === 'string' && d.length > 0) {
            const stroke = (inProps as { stroke?: unknown }).stroke;
            const fill = (inProps as { fill?: unknown }).fill;
            const strokeW = (inProps as { strokeWidth?: unknown }).strokeWidth;
            const swNum = typeof strokeW === 'number' ? strokeW : (typeof strokeW === 'string' ? parseFloat(strokeW) : undefined);
            (adapted as { ref?: unknown }).ref = _buildSvgPathRef(
                d,
                typeof fill === 'string' ? fill : undefined,
                typeof stroke === 'string' ? stroke : undefined,
                Number.isFinite(swNum) ? (swNum as number) : undefined,
                undefined, undefined,  // viewBox not on <path>; comes from parent <svg>
                (adapted as { ref?: unknown }).ref,
            );
        }
    }

    // Bundles set `<svg width="18" height="13">` — without this they
    // collapse to 0×0 inside flex rows, dragging the row's measured width
    // down even though every Label child has a minWidth.
    if (tag === 'svg' || tag === 'img' || tag === 'image' || tag === 'rect' || tag === 'circle') {
        const w = (inProps as { width?: unknown }).width;
        const h = (inProps as { height?: unknown }).height;
        if (adapted.width === undefined && (typeof w === 'number' || typeof w === 'string')) {
            const n = typeof w === 'number' ? w : parseFloat(String(w));
            if (Number.isFinite(n)) adapted.width = n;
        }
        if (adapted.height === undefined && (typeof h === 'number' || typeof h === 'string')) {
            const n = typeof h === 'number' ? h : parseFloat(String(h));
            if (Number.isFinite(n)) adapted.height = n;
        }
    }

    // <canvas>: default the View widget background to transparent so the
    // parent shows through where the canvas commands don't paint. Pulp's
    // canvas widget is transparent at the surface level (#929), but the
    // setBackground we don't call leaves the View parent's bg paint
    // unchanged — and Pulp's default View bg paints opaque white. Without
    // this, every <canvas> covers its parent like a white sheet. Tracked
    // as pulp#964 — fix at the framework layer; this is the ports-side
    // workaround that unblocks bundle ports today.
    // No canvas bg — let parent's bg show through transparent regions

    if (target === 'Label') {
        // Text is in the varargs `children` (jsx-runtime-shim destructures
        // it out of props), or rarely as inProps.children when raw
        // createElement is used. Check both.
        const childText = (() => {
            for (const c of children) {
                if (typeof c === 'string' && c.length > 0) return c;
                if (typeof c === 'number') return String(c);
            }
            const c = (inProps as { children?: unknown }).children;
            if (typeof c === 'string') return c;
            if (typeof c === 'number') return String(c);
            return '';
        })();
        if (childText.length > 0 && adapted.minWidth === undefined && adapted.width === undefined) {
            const fontSize = (adapted.fontSize as number) ?? 14;
            const ls = (adapted.letterSpacing as number) ?? 0;
            const mw = Math.ceil(childText.length * (fontSize * 0.65 + ls));
            adapted.minWidth = mw;
            // Critical: prevent Yoga from shrinking the Label below its
            // intrinsic width when parent's allocated space is smaller.
            // Default Yoga flexShrink is 1 (shrinkable). Pinning to 0
            // matches CSS `white-space: nowrap` for chrome labels.
            if (adapted.flexShrink === undefined) adapted.flexShrink = 0;
        }
        // CSS color-inheritance gap: a nested <span>{m.label}</span> inside
        // a coloured button (`<button style={{color:'#fff'}}>`) inherits in
        // CSS but not through dom-adapter — the inner span maps to its own
        // Label widget, dom-adapter sees no `color` style on the span, and
        // the Label paints with Pulp's default text colour, which on a dark
        // popover background reads as invisible. The parent button's
        // textColor lives on a different widget and Pulp doesn't propagate
        // it. Default any text-bearing Label without an explicit textColor
        // to the chrome-text colour the rest of the editor uses (matches
        // the `color: 'rgba(255,255,255,0.85)'` default on every chrome
        // button and popover row in spectr-editor-extracted.js). Tracked
        // alongside spectr#61 / pulp#1323 (CSS :hover translation gap) —
        // same root issue, different CSS property.
        if (childText.length > 0 && adapted.textColor === undefined) {
            adapted.textColor = 'rgba(255,255,255,0.85)';
        }
    }

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
        const cacheKey = refCacheKey(userRef);
        if (tag === 'canvas') {
            const cached = cacheKey ? __canvasRefCache.get(cacheKey) : undefined;
            const callback = cached ?? ((instance: unknown) => {
                const lg = (globalThis as { __spectrLog?: (s: string) => void }).__spectrLog;
                if (lg) lg('[ref-cb-canvas] inst=' + (instance ? 'object' : 'null'));
                const cId = instance && (instance as { id?: string }).id;
                if (cId) armPointerOnce(cId as string);
                const wrapped = instance && (instance as { id?: string }).id
                    ? wrapCanvasInstance(instance as { id: string })
                    : instance;
                if (typeof userRef === 'function') userRef(wrapped);
                else if (userRef) (userRef as { current: unknown }).current = wrapped;
            });
            if (cacheKey && !cached) __canvasRefCache.set(cacheKey, callback);
            (adapted as { ref?: unknown }).ref = callback;
        } else {
            // Non-canvas refs: extracted code (FilterBank's wrapRef etc.)
            // calls `wrap.getBoundingClientRect()` for layout. Bridge
            // instances don't ship that method, so the call throws and
            // silently kills the rAF chain. Augment the instance with
            // HTMLElement-ish methods returning sensible bounds.
            const cached = cacheKey ? __nonCanvasRefCache.get(cacheKey) : undefined;
            const callback = cached ?? ((instance: unknown) => {
                const lg = (globalThis as { __spectrLog?: (s: string) => void }).__spectrLog;
                if (lg) lg('[ref-cb] tag=' + tag + ' inst=' + (instance ? 'object' : 'null'));
                if (instance && typeof instance === 'object') {
                    const inst = instance as Record<string, unknown>;
                    // Spectr #32 — pulp gates pointer-event dispatch behind an
                    // explicit registerPointer(id) call (parallel to
                    // registerHover for mouseenter/leave and registerClick for
                    // click). @pulp/react's prop-applier wires registerHover
                    // for hover events but never calls registerPointer, so
                    // onPointerDown/Move/Up handlers on a wrap div are
                    // installed in the JS dispatch table but never fired by
                    // the native View. Arm pointer dispatch here on every
                    // ref-mounted non-canvas instance — idempotent on the
                    // bridge side and keeps pointer-driven UX (FilterBank
                    // gain drag, dropdown overlay-click routing) alive.
                    const id = inst.id as string | undefined;
                    if (id) armPointerOnce(id);
                    if (typeof inst.getBoundingClientRect !== 'function') {
                        // The wrap div is App's main 1320×860 viewport (FilterBank
                        // fills it via position:absolute, inset:0). Earlier this
                        // shim returned h = 860 − 44 (header) − 56 (rail) = 760
                        // to "match" the squeezed area, but FilterBank's own
                        // canvases overlay the chrome via z-order, so the wrap
                        // really IS the full 1320×860. Returning 760 caused
                        // FilterBank's getGeom() to compute the minimap at
                        // canvas-y=700 instead of 800 and clipped its band-area
                        // calculations. Match the App's outer-div size so JS
                        // geometry and the native canvas widget agree.
                        inst.getBoundingClientRect = () => {
                            const r = {
                                x: 0, y: 0, left: 0, top: 0,
                                width: 1320, height: 860,
                                right: 1320, bottom: 860,
                            };
                            return { ...r, toJSON: () => r };
                        };
                        inst.clientWidth = 1320;
                        inst.clientHeight = 860;
                        inst.offsetWidth = 1320;
                        inst.offsetHeight = 860;
                    }
                }
                if (typeof userRef === 'function') userRef(instance);
                else if (userRef) (userRef as { current: unknown }).current = instance;
            });
            if (cacheKey && !cached) __nonCanvasRefCache.set(cacheKey, callback);
            (adapted as { ref?: unknown }).ref = callback;
        }
    }
    if (typeof inProps.value === 'string' || typeof inProps.value === 'number') {
        adapted.value = inProps.value;
    }

    // Drop className entirely — no global stylesheet on this lane.
    // (data-* attributes also get dropped.)

    // Text-bearing widgets (Label/Button/TextEditor): @pulp/react's asText
    // only handles a SINGLE string/number child — multi-fragment JSX like
    // `<span>{label} ▾</span>` becomes children=[label, ' ▾'] which asText
    // can't read, so it falls back to '' and the widget renders blank.
    // Concatenate string/number fragments so asText sees one string.
    if (target === 'Label' || target === 'Button' || target === 'TextEditor') {
        let allStrings = true;
        let combined = '';
        for (const c of children) {
            if (typeof c === 'string' || typeof c === 'number') combined += String(c);
            else { allStrings = false; break; }
        }
        if (allStrings && children.length > 1 && combined.length > 0) {
            // Re-run minWidth calc on combined text so Label sizes correctly.
            if (target === 'Label' && adapted.minWidth === undefined && adapted.width === undefined) {
                const fontSize = (adapted.fontSize as number) ?? 14;
                const ls = (adapted.letterSpacing as number) ?? 0;
                adapted.minWidth = Math.ceil(combined.length * (fontSize * 0.65 + ls));
                if (adapted.flexShrink === undefined) adapted.flexShrink = 0;
            }
            return pulpCreateElement(target as never, adapted as never, combined);
        }
    }

    // String/number children of a non-text-bearing parent (View, Row,
    // Col, Panel, etc.) get auto-wrapped by @pulp/react's host config
    // into synthetic Labels with NO minWidth — and those Labels collapse
    // to 0 width in v0.59.0's TextShaper (#957 fix is on main but not
    // in any released SDK yet). Wrap them here instead so they go
    // through our minWidth path. Only fires when target is not Label /
    // Button / TextEditor, which already accept string children directly.
    if (target !== 'Label' && target !== 'Button' && target !== 'TextEditor') {
        const wrapped: ReactNode[] = [];
        let txtIdx = 0;
        // Inherit parent's typography to wrapped Labels — Pulp doesn't
        // propagate textColor/fontSize/letterSpacing from parent View
        // to child Label widgets (CSS-style inheritance gap), so we have
        // to push it down at adapt time.
        const inheritedColor = adapted.textColor as string | undefined;
        const inheritedFontSize = (adapted.fontSize as number | undefined);
        const inheritedLetterSpacing = (adapted.letterSpacing as number | undefined);
        const fs = inheritedFontSize ?? 14;
        const ls = inheritedLetterSpacing ?? 0;
        for (const c of children) {
            if (typeof c === 'string' || typeof c === 'number') {
                const txt = String(c);
                if (txt.length === 0) continue;
                const mw = Math.ceil(txt.length * (fs * 0.65 + ls));
                const labelProps: Record<string, unknown> = {
                    minWidth: mw,
                    flexShrink: 0,
                    key: '__txt_' + txtIdx++,
                };
                if (inheritedColor !== undefined) labelProps.textColor = inheritedColor;
                if (inheritedFontSize !== undefined) labelProps.fontSize = inheritedFontSize;
                if (inheritedLetterSpacing !== undefined) labelProps.letterSpacing = inheritedLetterSpacing;
                wrapped.push(
                    pulpCreateElement('Label' as never, labelProps as never, txt),
                );
            } else {
                wrapped.push(c);
            }
        }
        return pulpCreateElement(target as never, adapted as never, ...wrapped);
    }

    return pulpCreateElement(target as never, adapted as never, ...children);
}

/// Re-exports so the extracted code can import them as if from React.
export { useState, useEffect, useRef, useCallback, useMemo, Fragment } from 'react';
