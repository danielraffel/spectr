// Bundled snapshot of Pulp's @pulp/react bridge surface (setX + createX).
// Used when --bridge-list is not provided. Keep this in sync with the
// dom-adapter. See `tools/pulp-css-analyze/README.md` for refresh notes.

export const DEFAULT_BRIDGE_FUNCTIONS: readonly string[] = [
  'setBackground',
  'setTextColor',
  'setOpacity',
  'setFlex',
  'setBorder',
  'setBorderSide',
  'setBorderRadius',
  'setPosition',
  'setTop',
  'setLeft',
  'setRight',
  'setBottom',
  'setZIndex',
  'setVisible',
  'setFontSize',
  'setFontFamily',
  'setFontWeight',
  'setLetterSpacing',
  'setLineHeight',
  'setTextAlign',
  'createCol',
  'createRow',
  'createPanel',
  'createLabel',
  'createButton',
  'createCanvas',
  'createKnob',
  'createFader',
  'createSpectrum',
  'createCheckbox',
  'createCombo',
];

/** Returns the raw setter/factory list in canonical order. */
export function getDefaultBridgeFunctions(): string[] {
  return [...DEFAULT_BRIDGE_FUNCTIONS];
}
