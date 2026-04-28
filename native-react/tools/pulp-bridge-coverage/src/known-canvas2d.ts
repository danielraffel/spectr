// W3C Canvas 2D API surface (spec-driven, not Pulp-specific).
// Source: https://html.spec.whatwg.org/multipage/canvas.html#2dcontext
// Last reviewed: 2026-04-28 against the Living Standard.
//
// Each entry is a property name accessed off a CanvasRenderingContext2D
// instance. Subset entries cover the common cases bundles like Spectr's
// editor.html actually call. When a new version of the spec adds APIs,
// extend this list — keep it the SINGLE source of truth.

export interface CanvasApiEntry {
    name: string;
    kind: 'method' | 'property';
    spec: string;       // section anchor in the WHATWG HTML spec
    notes?: string;
}

// Methods + properties listed alphabetically inside each spec section.
export const CANVAS_2D_API: CanvasApiEntry[] = [
    // Shape drawing — paths
    { name: 'beginPath',          kind: 'method',   spec: '#dom-context-2d-beginpath' },
    { name: 'closePath',          kind: 'method',   spec: '#dom-context-2d-closepath' },
    { name: 'moveTo',             kind: 'method',   spec: '#dom-context-2d-moveto' },
    { name: 'lineTo',             kind: 'method',   spec: '#dom-context-2d-lineto' },
    { name: 'bezierCurveTo',      kind: 'method',   spec: '#dom-context-2d-beziercurveto' },
    { name: 'quadraticCurveTo',   kind: 'method',   spec: '#dom-context-2d-quadraticcurveto' },
    { name: 'arc',                kind: 'method',   spec: '#dom-context-2d-arc' },
    { name: 'arcTo',              kind: 'method',   spec: '#dom-context-2d-arcto' },
    { name: 'ellipse',            kind: 'method',   spec: '#dom-context-2d-ellipse' },
    { name: 'rect',               kind: 'method',   spec: '#dom-context-2d-rect' },
    { name: 'roundRect',          kind: 'method',   spec: '#dom-context-2d-roundrect' },

    // Drawing rectangles
    { name: 'fillRect',           kind: 'method',   spec: '#dom-context-2d-fillrect' },
    { name: 'strokeRect',         kind: 'method',   spec: '#dom-context-2d-strokerect' },
    { name: 'clearRect',          kind: 'method',   spec: '#dom-context-2d-clearrect' },

    // Drawing paths
    { name: 'fill',               kind: 'method',   spec: '#dom-context-2d-fill' },
    { name: 'stroke',             kind: 'method',   spec: '#dom-context-2d-stroke' },
    { name: 'clip',               kind: 'method',   spec: '#dom-context-2d-clip' },
    { name: 'isPointInPath',      kind: 'method',   spec: '#dom-context-2d-ispointinpath' },
    { name: 'isPointInStroke',    kind: 'method',   spec: '#dom-context-2d-ispointinstroke' },

    // Text
    { name: 'fillText',           kind: 'method',   spec: '#dom-context-2d-filltext' },
    { name: 'strokeText',         kind: 'method',   spec: '#dom-context-2d-stroketext' },
    { name: 'measureText',        kind: 'method',   spec: '#dom-context-2d-measuretext' },
    { name: 'font',               kind: 'property', spec: '#dom-context-2d-font' },
    { name: 'textAlign',          kind: 'property', spec: '#dom-context-2d-textalign' },
    { name: 'textBaseline',       kind: 'property', spec: '#dom-context-2d-textbaseline' },
    { name: 'direction',          kind: 'property', spec: '#dom-context-2d-direction' },

    // Fill / stroke styles
    { name: 'fillStyle',          kind: 'property', spec: '#dom-context-2d-fillstyle' },
    { name: 'strokeStyle',        kind: 'property', spec: '#dom-context-2d-strokestyle' },
    { name: 'createLinearGradient', kind: 'method', spec: '#dom-context-2d-createlineargradient' },
    { name: 'createRadialGradient', kind: 'method', spec: '#dom-context-2d-createradialgradient' },
    { name: 'createConicGradient',  kind: 'method', spec: '#dom-context-2d-createconicgradient' },
    { name: 'createPattern',      kind: 'method',   spec: '#dom-context-2d-createpattern' },

    // Line styles
    { name: 'lineWidth',          kind: 'property', spec: '#dom-context-2d-linewidth' },
    { name: 'lineCap',            kind: 'property', spec: '#dom-context-2d-linecap' },
    { name: 'lineJoin',           kind: 'property', spec: '#dom-context-2d-linejoin' },
    { name: 'miterLimit',         kind: 'property', spec: '#dom-context-2d-miterlimit' },
    { name: 'lineDashOffset',     kind: 'property', spec: '#dom-context-2d-linedashoffset' },
    { name: 'getLineDash',        kind: 'method',   spec: '#dom-context-2d-getlinedash' },
    { name: 'setLineDash',        kind: 'method',   spec: '#dom-context-2d-setlinedash' },

    // Shadows
    { name: 'shadowBlur',         kind: 'property', spec: '#dom-context-2d-shadowblur' },
    { name: 'shadowColor',        kind: 'property', spec: '#dom-context-2d-shadowcolor' },
    { name: 'shadowOffsetX',      kind: 'property', spec: '#dom-context-2d-shadowoffsetx' },
    { name: 'shadowOffsetY',      kind: 'property', spec: '#dom-context-2d-shadowoffsety' },

    // Images
    { name: 'drawImage',          kind: 'method',   spec: '#dom-context-2d-drawimage' },
    { name: 'createImageData',    kind: 'method',   spec: '#dom-context-2d-createimagedata' },
    { name: 'getImageData',       kind: 'method',   spec: '#dom-context-2d-getimagedata' },
    { name: 'putImageData',       kind: 'method',   spec: '#dom-context-2d-putimagedata' },

    // Transforms
    { name: 'getTransform',       kind: 'method',   spec: '#dom-context-2d-gettransform' },
    { name: 'setTransform',       kind: 'method',   spec: '#dom-context-2d-settransform' },
    { name: 'transform',          kind: 'method',   spec: '#dom-context-2d-transform' },
    { name: 'translate',          kind: 'method',   spec: '#dom-context-2d-translate' },
    { name: 'rotate',             kind: 'method',   spec: '#dom-context-2d-rotate' },
    { name: 'scale',              kind: 'method',   spec: '#dom-context-2d-scale' },
    { name: 'resetTransform',     kind: 'method',   spec: '#dom-context-2d-resettransform' },

    // State
    { name: 'save',               kind: 'method',   spec: '#dom-context-2d-save' },
    { name: 'restore',            kind: 'method',   spec: '#dom-context-2d-restore' },
    { name: 'reset',              kind: 'method',   spec: '#dom-context-2d-reset' },
    { name: 'globalAlpha',        kind: 'property', spec: '#dom-context-2d-globalalpha' },
    { name: 'globalCompositeOperation', kind: 'property', spec: '#dom-context-2d-globalcompositeoperation' },
    { name: 'imageSmoothingEnabled',    kind: 'property', spec: '#dom-context-2d-imagesmoothingenabled' },
    { name: 'imageSmoothingQuality',    kind: 'property', spec: '#dom-context-2d-imagesmoothingquality' },
    { name: 'filter',             kind: 'property', spec: '#dom-context-2d-filter' },

    // Canvas-element accessors (off ctx, not the canvas element itself)
    { name: 'canvas',             kind: 'property', spec: '#dom-context-2d-canvas' },
];

export const CANVAS_2D_BY_NAME: Map<string, CanvasApiEntry> =
    new Map(CANVAS_2D_API.map(e => [e.name, e]));
