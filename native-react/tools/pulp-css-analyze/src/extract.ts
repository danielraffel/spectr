// AST walker that finds every literal `style={{...}}` object in a JS
// bundle — both modern jsx-runtime calls (`jsx`, `jsxs`, `jsxDEV`) and
// classic `React.createElement` / `createElement` calls.
//
// Output: `Map<cssProp, Set<value>>` plus a separate occurrence count
// (number of style objects in which the prop appeared).
//
// Intentionally conservative: skips computed keys and dynamic values,
// since we want a *static* coverage report and any AOT-undecidable bit
// would just inflate the unmapped count with `?` placeholders.

import { parse, type ParserOptions } from '@babel/parser';
import _traverse from '@babel/traverse';
import type { NodePath } from '@babel/traverse';
import type {
  CallExpression,
  Expression,
  Node,
  ObjectExpression,
  ObjectProperty,
  PrivateName,
  SpreadElement,
} from '@babel/types';

// `@babel/traverse` is published as CJS; under ESM it's exposed as
// `{ default }`. Be defensive about either shape.
const traverse: typeof _traverse = (
  (_traverse as unknown as { default?: typeof _traverse }).default ?? _traverse
);

export interface ExtractResult {
  /** prop → set of distinct literal values (stringified) */
  values: Map<string, Set<string>>;
  /** prop → number of style objects in which it appeared */
  occurrences: Map<string, number>;
  /** total number of style objects walked */
  styleObjectCount: number;
  /** number of style props that were dynamic / unknown (skipped) */
  dynamicSkippedCount: number;
}

const JSX_RUNTIME_CALLEES = new Set(['jsx', 'jsxs', 'jsxDEV']);
const CREATE_ELEMENT_CALLEES = new Set(['createElement', 'h']);

const PARSER_OPTIONS: ParserOptions = {
  sourceType: 'unambiguous',
  allowReturnOutsideFunction: true,
  allowImportExportEverywhere: true,
  allowAwaitOutsideFunction: true,
  errorRecovery: true,
  plugins: [
    'jsx',
    'typescript',
    'classProperties',
    'classPrivateProperties',
    'classPrivateMethods',
    'optionalChaining',
    'nullishCoalescingOperator',
    'numericSeparator',
    'topLevelAwait',
    'dynamicImport',
    'importAssertions',
  ],
};

/**
 * Parse `source` and walk every style-prop object literal.
 *
 * Returned counts treat each style object as a single "occurrence" for
 * the purposes of ranking unmapped props by frequency, but distinct
 * values are still deduplicated across the whole bundle.
 */
export function extractStyles(source: string): ExtractResult {
  const ast = parse(source, PARSER_OPTIONS);

  const values = new Map<string, Set<string>>();
  const occurrences = new Map<string, number>();
  let styleObjectCount = 0;
  let dynamicSkippedCount = 0;

  traverse(ast, {
    CallExpression(path: NodePath<CallExpression>) {
      const styleArg = pickStyleArgument(path.node);
      if (!styleArg) return;
      if (styleArg.type !== 'ObjectExpression') {
        // e.g. `style: someVariable` — skip
        return;
      }
      styleObjectCount += 1;
      const seenInThisObject = new Set<string>();
      for (const prop of styleArg.properties) {
        const extracted = readObjectProperty(prop);
        if (!extracted) {
          dynamicSkippedCount += 1;
          continue;
        }
        const { key, value } = extracted;
        seenInThisObject.add(key);
        let bag = values.get(key);
        if (!bag) {
          bag = new Set<string>();
          values.set(key, bag);
        }
        if (value !== undefined) bag.add(value);
      }
      for (const key of seenInThisObject) {
        occurrences.set(key, (occurrences.get(key) ?? 0) + 1);
      }
    },
  });

  return { values, occurrences, styleObjectCount, dynamicSkippedCount };
}

/** Extract the style-object argument from a jsx() / createElement() call. */
function pickStyleArgument(call: CallExpression): Expression | null {
  const callee = call.callee;
  let calleeName: string | null = null;
  if (callee.type === 'Identifier') calleeName = callee.name;
  else if (callee.type === 'MemberExpression' && callee.property.type === 'Identifier') {
    calleeName = callee.property.name;
  }
  if (!calleeName) return null;

  const isJsxRuntime = JSX_RUNTIME_CALLEES.has(calleeName);
  const isCreateElement = CREATE_ELEMENT_CALLEES.has(calleeName);
  if (!isJsxRuntime && !isCreateElement) return null;

  // Both shapes: arg[0] = type, arg[1] = props object.
  const propsArg = call.arguments[1];
  if (!propsArg || propsArg.type !== 'ObjectExpression') return null;

  const styleProp = propsArg.properties.find((p): p is ObjectProperty => {
    if (p.type !== 'ObjectProperty') return false;
    return getStaticKeyName(p.key) === 'style';
  });
  if (!styleProp) return null;

  const v = styleProp.value;
  if (!isExpression(v)) return null;
  return v;
}

function isExpression(n: Node): n is Expression {
  // PatternLike values appear in destructuring; we don't care about them.
  // Treat anything that isn't a known non-expression node as an expression
  // for our narrow purposes (we only consume ObjectExpression downstream).
  return (
    n.type !== 'RestElement' &&
    n.type !== 'ObjectPattern' &&
    n.type !== 'ArrayPattern' &&
    n.type !== 'AssignmentPattern'
  );
}

interface ReadProp { key: string; value: string | undefined }

function readObjectProperty(prop: ObjectProperty | SpreadElement | Node): ReadProp | null {
  if (prop.type !== 'ObjectProperty') return null;
  if (prop.computed) return null;
  const keyName = getStaticKeyName(prop.key);
  if (!keyName) return null;
  const value = prop.value;
  let valStr: string | undefined;
  switch (value.type) {
    case 'StringLiteral':
      valStr = value.value;
      break;
    case 'NumericLiteral':
      valStr = String(value.value);
      break;
    case 'BooleanLiteral':
      valStr = String(value.value);
      break;
    case 'NullLiteral':
      valStr = 'null';
      break;
    case 'TemplateLiteral':
      // Static template literals (no expressions) — we can capture them.
      if (value.expressions.length === 0 && value.quasis.length === 1) {
        valStr = value.quasis[0]?.value.cooked ?? value.quasis[0]?.value.raw;
      } else {
        valStr = undefined; // dynamic, but we still tally the prop
      }
      break;
    case 'UnaryExpression':
      if (value.operator === '-' && value.argument.type === 'NumericLiteral') {
        valStr = String(-value.argument.value);
      }
      break;
    default:
      // dynamic — record the prop but not the value
      valStr = undefined;
  }
  return { key: keyName, value: valStr };
}

function getStaticKeyName(key: Expression | PrivateName): string | null {
  if (key.type === 'Identifier') return key.name;
  if (key.type === 'StringLiteral') return key.value;
  if (key.type === 'NumericLiteral') return String(key.value);
  return null;
}

/** Helper: pretty-format up to `n` distinct values for a given prop. */
export function sampleValues(set: Set<string> | undefined, n = 3): string {
  if (!set || set.size === 0) return '—';
  const arr = Array.from(set).slice(0, n);
  return arr.map(v => (v.length > 40 ? v.slice(0, 37) + '…' : v)).join(', ');
}

// (Type re-export so consumers don't need to dig into ../node_modules.)
export type { ObjectExpression };
