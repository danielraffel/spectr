import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';

const fixture = JSON.parse(fs.readFileSync(new URL('./fixtures/parity-state-canonical.json', import.meta.url)));
const expected = fixture.stateSha256;
delete fixture.stateSha256;
const canonical = value => Array.isArray(value) ? `[${value.map(canonical).join(',')}]`
  : value && typeof value === 'object' ? `{${Object.keys(value).sort().map(k => `${JSON.stringify(k)}:${canonical(value[k])}`).join(',')}}`
  : JSON.stringify(value);
assert.equal(crypto.createHash('sha256').update(canonical(fixture)).digest('hex'), expected);
console.log('PASS: parity state canonical fixture');
