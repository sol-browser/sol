import test from 'node:test';
import assert from 'node:assert/strict';
import { DEFAULT_THEME, parseSettingsSnapshot, parseSnapshot } from '../packages/browser-contracts/src/index.ts';

const settings = hours => ({ theme:DEFAULT_THEME, ready:true, spaceName:'Personal',spaceId:'1'.repeat(36),version:'0.5.1',chromiumVersion:'154.0.8037.98',
  defaultBrowser:{ installed:true,registered:true,isDefault:null },preferencesError:'',keyboard:[],
  preferences:{ schema:1,searchEngine:'google',downloadDirectory:'',askDownload:false,startup:'restore',homepage:'https://example.com/',shortcuts:{},autoArchiveHours:hours } });

test('auto-archive accepts only the supported persisted intervals and disabled state', () => {
  for (const hours of [0,12,24,48,168]) assert.equal(parseSettingsSnapshot(JSON.stringify(settings(hours))).preferences.autoArchiveHours,hours);
  for (const hours of [-1,1,12.5,169,'12',null]) assert.throws(() => parseSettingsSnapshot(JSON.stringify(settings(hours))));
});
test('older preferences without an archive setting remain readable', () => {
  const value=settings(12); delete value.preferences.autoArchiveHours;
  assert.equal(parseSettingsSnapshot(JSON.stringify(value)).preferences.autoArchiveHours,undefined);
});
test('fullscreen presentation state requires a boolean', () => {
  const value={ revision:1,canGoBack:false,canGoForward:false,probeMode:false,browserFullscreen:true,
    workspaceEnabled:true,schemaVersion:2,treeRevision:0,ready:true,error:'',activeId:'',
    sidebarVisible:true,contentBounds:{x:264,y:31,width:912,height:736},
    url:'',isLoading:false,nodes:[] };
  assert.equal(parseSnapshot(JSON.stringify(value)).browserFullscreen,true);
  assert.equal(parseSnapshot(JSON.stringify({...value,browserFullscreen:false})).browserFullscreen,false);
  assert.throws(() => parseSnapshot(JSON.stringify({...value,browserFullscreen:'true'})),/Invalid fullscreen state/);
});
