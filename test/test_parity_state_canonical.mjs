import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';

const fixture = JSON.parse(fs.readFileSync(new URL('./fixtures/parity-state-canonical.json', import.meta.url)));
const expected = fixture.stateSha256;
delete fixture.stateSha256;
const numberText = value => {
  const text = String(value);
  if (!/[eE]/.test(text)) return text;
  const [coefficient, exponentText] = text.toLowerCase().split('e');
  const exponent = Number(exponentText);
  const sign = coefficient.startsWith('-') ? '-' : '';
  const digits = coefficient.replace(/^[-+]/, '').replace('.', '');
  const decimal = (coefficient.replace(/^[-+]/, '').indexOf('.') < 0
    ? digits.length : coefficient.replace(/^[-+]/, '').indexOf('.')) + exponent;
  if (decimal <= 0) return `${sign}0.${'0'.repeat(-decimal)}${digits}`;
  if (decimal >= digits.length) return `${sign}${digits}${'0'.repeat(decimal - digits.length)}`;
  return `${sign}${digits.slice(0, decimal)}.${digits.slice(decimal)}`;
};
const canonical = value => Array.isArray(value) ? `[${value.map(canonical).join(',')}]`
  : value && typeof value === 'object' ? `{${Object.keys(value).sort().map(k => `${JSON.stringify(k)}:${canonical(value[k])}`).join(',')}}`
  : typeof value === 'number' ? numberText(value)
  : JSON.stringify(value);
assert.equal(crypto.createHash('sha256').update(canonical(fixture)).digest('hex'), expected);
console.log('PASS: parity state canonical fixture');
