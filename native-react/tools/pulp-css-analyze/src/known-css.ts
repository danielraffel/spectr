// Static cross-reference between CSS properties and Pulp bridge setters.
// `mapped`           — the dom-adapter has a known lowering path.
// `intentional-drop` — the dom-adapter consciously drops it (e.g. cursor,
//                      transition) because the GPU/native runtime has no
//                      analogue or it's a no-op in our context.
// `unmapped`         — neither — silent drop, candidate for a framework
//                      gap or an adapter-side lowering.

export type MappingStatus = 'mapped' | 'intentional-drop' | 'unmapped';

export interface CssMapping {
  prop: string;
  status: MappingStatus;
  /** Bridge setter(s) that consume the value when mapped. */
  bridgeSetters?: string[];
  /** Free-text note (used in the report's "Suggested fixes" section). */
  note?: string;
}

// Source of truth for adapter behavior. Property names are normalized to
// camelCase (matching React style-prop convention) but lowercase
// "kebab-case" lookups are also accepted in `lookupCssMapping`.
const MAPPINGS: CssMapping[] = [
  // -- Layout / box ----------------------------------------------------
  { prop: 'display',         status: 'mapped',           bridgeSetters: ['setVisible', 'setFlex'], note: '"none" → setVisible(false); "flex"/"block" → flex container.' },
  { prop: 'flex',            status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'flexDirection',   status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'flexGrow',        status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'flexShrink',      status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'flexBasis',       status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'flexWrap',        status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'alignItems',      status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'alignSelf',       status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'alignContent',    status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'justifyContent',  status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'gap',             status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'rowGap',          status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'columnGap',       status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'width',           status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'height',          status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'minWidth',        status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'minHeight',       status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'maxWidth',        status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'maxHeight',       status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'padding',         status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'paddingTop',      status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'paddingBottom',   status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'paddingLeft',     status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'paddingRight',    status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'margin',          status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'marginTop',       status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'marginBottom',    status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'marginLeft',      status: 'mapped',           bridgeSetters: ['setFlex'] },
  { prop: 'marginRight',     status: 'mapped',           bridgeSetters: ['setFlex'] },

  // -- Position --------------------------------------------------------
  { prop: 'position',        status: 'mapped',           bridgeSetters: ['setPosition'] },
  { prop: 'top',             status: 'mapped',           bridgeSetters: ['setTop'] },
  { prop: 'left',            status: 'mapped',           bridgeSetters: ['setLeft'] },
  { prop: 'right',           status: 'mapped',           bridgeSetters: ['setRight'] },
  { prop: 'bottom',          status: 'mapped',           bridgeSetters: ['setBottom'] },
  { prop: 'inset',           status: 'mapped',           bridgeSetters: ['setTop', 'setLeft', 'setRight', 'setBottom'] },
  { prop: 'zIndex',          status: 'mapped',           bridgeSetters: ['setZIndex'] },
  { prop: 'visibility',      status: 'mapped',           bridgeSetters: ['setVisible'] },

  // -- Color / fill ----------------------------------------------------
  { prop: 'background',      status: 'mapped',           bridgeSetters: ['setBackground'] },
  { prop: 'backgroundColor', status: 'mapped',           bridgeSetters: ['setBackground'] },
  { prop: 'backgroundImage', status: 'mapped',           bridgeSetters: ['setBackground'], note: 'linear-gradient strings parsed by setBackground.' },
  { prop: 'color',           status: 'mapped',           bridgeSetters: ['setTextColor'] },
  { prop: 'opacity',         status: 'mapped',           bridgeSetters: ['setOpacity'] },

  // -- Border ----------------------------------------------------------
  { prop: 'border',          status: 'mapped',           bridgeSetters: ['setBorder'] },
  { prop: 'borderWidth',     status: 'mapped',           bridgeSetters: ['setBorder'] },
  { prop: 'borderColor',     status: 'mapped',           bridgeSetters: ['setBorder'] },
  { prop: 'borderStyle',     status: 'mapped',           bridgeSetters: ['setBorder'] },
  { prop: 'borderTop',       status: 'mapped',           bridgeSetters: ['setBorderSide'] },
  { prop: 'borderRight',     status: 'mapped',           bridgeSetters: ['setBorderSide'] },
  { prop: 'borderBottom',    status: 'mapped',           bridgeSetters: ['setBorderSide'] },
  { prop: 'borderLeft',      status: 'mapped',           bridgeSetters: ['setBorderSide'] },
  { prop: 'borderRadius',    status: 'mapped',           bridgeSetters: ['setBorderRadius'] },

  // -- Text / typography ----------------------------------------------
  { prop: 'fontSize',        status: 'mapped',           bridgeSetters: ['setFontSize'] },
  { prop: 'fontFamily',      status: 'mapped',           bridgeSetters: ['setFontFamily'] },
  { prop: 'fontWeight',      status: 'mapped',           bridgeSetters: ['setFontWeight'] },
  { prop: 'letterSpacing',   status: 'mapped',           bridgeSetters: ['setLetterSpacing'] },
  { prop: 'lineHeight',      status: 'mapped',           bridgeSetters: ['setLineHeight'] },
  { prop: 'textAlign',       status: 'mapped',           bridgeSetters: ['setTextAlign'] },

  // -- Intentional drops ----------------------------------------------
  { prop: 'cursor',           status: 'intentional-drop', note: 'No native cursor system; bridge has no setCursor.' },
  { prop: 'pointerEvents',    status: 'intentional-drop', note: 'Hit-testing handled at widget granularity, not via CSS.' },
  { prop: 'userSelect',       status: 'intentional-drop', note: 'Native UI does not surface CSS text-selection.' },
  { prop: 'transition',       status: 'intentional-drop', note: 'Animations are widget-driven, not CSS-driven.' },
  { prop: 'transitionProperty', status: 'intentional-drop' },
  { prop: 'transitionDuration', status: 'intentional-drop' },
  { prop: 'animation',        status: 'intentional-drop' },
  { prop: 'willChange',       status: 'intentional-drop' },
  { prop: 'overflow',         status: 'intentional-drop', note: 'setClip not yet exposed; tracked separately.' },
  { prop: 'overflowX',        status: 'intentional-drop' },
  { prop: 'overflowY',        status: 'intentional-drop' },
  { prop: 'outline',          status: 'intentional-drop', note: 'Focus indicators are widget-driven.' },
  { prop: 'outlineOffset',    status: 'intentional-drop' },
  { prop: 'whiteSpace',       status: 'intentional-drop', note: 'TextShaper handles wrapping decisions.' },
  { prop: 'wordBreak',        status: 'intentional-drop' },
  { prop: 'wordWrap',         status: 'intentional-drop' },
  { prop: 'textTransform',    status: 'intentional-drop' },
  { prop: 'textDecoration',   status: 'intentional-drop' },
  { prop: 'fontVariant',      status: 'intentional-drop' },
  { prop: 'fontStyle',        status: 'intentional-drop' },
  { prop: 'boxSizing',        status: 'intentional-drop', note: 'Yoga uses border-box semantics by default.' },
];

const MAPPING_INDEX: Map<string, CssMapping> = (() => {
  const idx = new Map<string, CssMapping>();
  for (const m of MAPPINGS) {
    idx.set(canonical(m.prop), m);
  }
  return idx;
})();

/** Convert kebab-case to camelCase. Pass-through if already camel. */
export function camelCase(input: string): string {
  return input.replace(/-([a-z])/g, (_m, ch: string) => ch.toUpperCase());
}

/** Lowercase + camel for stable lookup. */
function canonical(p: string): string {
  return camelCase(p).toLowerCase();
}

/** Look up a CSS prop's mapping status; unknown → unmapped. */
export function lookupCssMapping(prop: string): CssMapping {
  const hit = MAPPING_INDEX.get(canonical(prop));
  if (hit) return hit;
  return { prop: camelCase(prop), status: 'unmapped' };
}

/** All known CSS mapping rows (read-only view). */
export function getAllMappings(): readonly CssMapping[] {
  return MAPPINGS;
}
