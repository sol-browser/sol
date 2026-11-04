import test from 'node:test';
import assert from 'node:assert/strict';
import {validSpaceIcon} from '../packages/browser-contracts/src/index.ts';
import {spaceIconFromText} from '../apps/browser-ui/src/space-icon-model.ts';

test('whole Windows emoji sequences survive grapheme selection and bounded validation',()=>{
 for(const icon of ['💡','👩‍👩‍👧‍👦','🧑🏽‍💻','🏳️‍🌈','🇺🇦','❤️','1️⃣']){
  assert(validSpaceIcon(icon),icon);assert.equal(spaceIconFromText(icon+'🌿'),icon);
 }
 assert.equal(spaceIconFromText(''),'');assert.equal(spaceIconFromText(' '),undefined);
 for(const invalid of ['\ud800','\udc00','a b','x\n','\0','x'.repeat(17),{},null])assert(!validSpaceIcon(invalid));
 assert(validSpaceIcon(''));
});
