import test from 'node:test';
import assert from 'node:assert/strict';
import {createThemePreviewQueue} from '../apps/browser-ui/src/theme-preview.ts';
const tick=()=>new Promise(resolve=>setImmediate(resolve));
test('closing the popup waits for the final save fence, and keeps it open on failure',async()=>{
 const release=[];const queue=createThemePreviewQueue(()=>new Promise((resolve,reject)=>release.push({resolve,reject})));
 queue(1,false);queue(2,true);let closed=false;const drained=queue.drain().then(()=>{closed=true;});
 release.shift().resolve();await tick();assert.equal(closed,false);release.shift().resolve();await drained;assert.equal(closed,true);
 queue(3,true);const failure=assert.rejects(queue.drain(),/save failed/);release.shift().reject(Error('save failed'));await failure;
});
test('slow native bridge retains latest preview and final save without a request backlog',async()=>{
  const sent=[],release=[];
  const queue=createThemePreviewQueue((v,save)=>{sent.push([v,save]);return new Promise(resolve=>release.push(resolve));});
  queue(0,false);for(let i=1;i<500;i++)queue(i,false);queue(500,true);
  assert.deepEqual(sent,[[0,false]]);
  release.shift()();await tick();assert.deepEqual(sent,[[0,false],[500,true]]);
  release.shift()();await tick();assert.equal(sent.length,2);
});
test('save fences survive a new drag and failed older requests do not strand final save',async()=>{
  const sent=[],release=[],errors=[];
  const queue=createThemePreviewQueue((v,save)=>{sent.push([v,save]);return new Promise((resolve,reject)=>release.push({resolve,reject}));},e=>errors.push(e));
  queue(1,false);queue(2,true);queue(3,false);queue(4,false);queue(5,true);
  release.shift().reject(Error('old preview'));await tick();assert.deepEqual(sent,[[1,false],[2,true]]);
  release.shift().resolve();await tick();assert.deepEqual(sent,[[1,false],[2,true],[5,true]]);
  release.shift().resolve();await tick();assert.deepEqual(errors,[undefined]);
});
