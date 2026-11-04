import test from 'node:test';
import assert from 'node:assert/strict';
import {EMOJI_CATALOG} from '../apps/browser-ui/src/emoji-catalog.ts';
import {findEmoji,EMOJI_GROUPS} from '../apps/browser-ui/src/emoji-picker-model.ts';
import {validSpaceIcon} from '../packages/browser-contracts/src/index.ts';

test('offline catalog contains complete, unique, storage-safe emoji across all categories',()=>{
 assert.equal(EMOJI_CATALOG.length,3944);assert.equal(new Set(EMOJI_CATALOG.map(e=>e[0])).size,3944);
 for(const entry of EMOJI_CATALOG){assert(validSpaceIcon(entry[0]),entry[0]);assert(entry[1]);assert(entry[3]>=0&&entry[3]<EMOJI_GROUPS.length);}
 for(let i=0;i<EMOJI_GROUPS.length;i++)assert(findEmoji(EMOJI_CATALOG,'',i).length>50);
 for(const emoji of ['👩‍👩‍👧‍👦','🧑🏽‍💻','❤️','💡'])assert(EMOJI_CATALOG.some(e=>e[0]===emoji));
});
test('emoji search accepts Russian, English and Unicode independently of the selected category',()=>{
 for(const query of ['лампочка','light bulb','💡'])assert(findEmoji(EMOJI_CATALOG,query,0).some(e=>e[0]==='💡'),query);
 assert(findEmoji(EMOJI_CATALOG,'семья',0).some(e=>e[0]==='👩‍👩‍👧‍👦'));
 assert(findEmoji(EMOJI_CATALOG,'technologist',0).some(e=>e[0]==='🧑🏽‍💻'));
 assert.equal(findEmoji(EMOJI_CATALOG,'no-such-emoji-293873',0).length,0);
});
