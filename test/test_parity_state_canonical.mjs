import assert from 'node:assert/strict';
import fs from 'node:fs';
import {canonical, stateDigest} from '../tools/parity_state_canonical.mjs';

const fixture = JSON.parse(fs.readFileSync(new URL('./fixtures/parity-state-canonical.json', import.meta.url)));
assert.equal(stateDigest(fixture), fixture.stateSha256);
assert.throws(() => canonical({unsafe: 2 ** 53}), /unsafe integer/);
console.log('PASS: parity state canonical fixture');
