import crypto from 'node:crypto';

const numberText = value => {
  const text = String(value);
  if (!/[eE]/.test(text)) return text;
  const [coefficient, exponentText] = text.toLowerCase().split('e');
  const exponent = Number(exponentText);
  const sign = coefficient.startsWith('-') ? '-' : '';
  const digits = coefficient.replace(/^[-+]/, '').replace('.', '');
  const base = coefficient.replace(/^[-+]/, '');
  const decimal = (base.indexOf('.') < 0 ? digits.length : base.indexOf('.')) + exponent;
  if (decimal <= 0) return `${sign}0.${'0'.repeat(-decimal)}${digits}`;
  if (decimal >= digits.length) return `${sign}${digits}${'0'.repeat(decimal - digits.length)}`;
  return `${sign}${digits.slice(0, decimal)}.${digits.slice(decimal)}`;
};

export const canonical = value => Array.isArray(value) ? `[${value.map(canonical).join(',')}]`
  : value && typeof value === 'object' ? `{${Object.keys(value).sort().map(k => `${JSON.stringify(k)}:${canonical(value[k])}`).join(',')}}`
  : typeof value === 'number' ? (() => {
    if (!Number.isFinite(value)) throw new TypeError('non-finite number is not canonical JSON');
    if (Number.isInteger(value) && !Number.isSafeInteger(value)) throw new RangeError('unsafe integer outside JavaScript safe range');
    return numberText(value);
  })()
  : JSON.stringify(value);

export const stateDigest = state => {
  const unsigned = {...state};
  delete unsigned.stateSha256;
  return crypto.createHash('sha256').update(canonical(unsigned)).digest('hex');
};
