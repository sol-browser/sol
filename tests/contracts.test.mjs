import test from 'node:test';
import assert from 'node:assert/strict';
import { parseSnapshot, parseSnapshotUpdate, parseSidebarMenuChoice, DEFAULT_THEME } from '../packages/browser-contracts/src/index.ts';
const folder = '11111111-1111-4111-8111-111111111111';
const tab = '22222222-2222-4222-8222-222222222222';
test('theme preview merges only validated theme/revision and retains the existing tree',()=>{
  const previous={revision:20,activeSpace:folder,nodes:[{id:tab}],theme:DEFAULT_THEME};
  const delta={themeOnly:true,revision:21,activeSpace:folder,theme:{...DEFAULT_THEME,hue:42}};
  const next=parseSnapshotUpdate(JSON.stringify(delta),previous);
  assert.equal(next.nodes,previous.nodes);assert.equal(next.theme.hue,42);assert.equal(next.revision,21);
  assert.equal(parseSnapshotUpdate(JSON.stringify({...delta,revision:19}),next),next);
  for(const bad of [{...delta,activeSpace:tab},{...delta,nodes:[]},{...delta,revision:NaN},{...delta,theme:{...delta.theme,hue:360}}])
    assert.throws(()=>parseSnapshotUpdate(JSON.stringify(bad),previous));
  assert.throws(()=>parseSnapshotUpdate(JSON.stringify(delta)));
});
test('live sidebar/developer tools resize merges only the validated layout',()=>{
  const box={x:0,y:0,width:10,height:10};
  const previous={revision:20,activeSpace:folder,nodes:[{id:tab}],theme:DEFAULT_THEME,devTools:{id:7,side:'right',extent:420,header:box,divider:box}};
  const delta={layoutOnly:true,revision:21,sidebarWidth:300,sidebarResizing:false,devTools:{id:7,side:'right',extent:500,header:box,divider:{...box,x:300}}};
  const next=parseSnapshotUpdate(JSON.stringify(delta),previous);
  assert.equal(next.nodes,previous.nodes);assert.equal(next.devTools.extent,500);assert.equal(next.revision,21);assert.equal(next.sidebarWidth,300);
  assert.equal(parseSnapshotUpdate(JSON.stringify({...delta,revision:19}),previous),previous);
  assert.equal(parseSnapshotUpdate(JSON.stringify({layoutOnly:true,revision:20,sidebarResizing:false}),previous).devTools,undefined);
  for(const bad of [{...delta,nodes:[]},{...delta,revision:-1},{...delta,devTools:{...delta.devTools,side:'top'}},{...delta,devicePreview:{id:1}},{...delta,sidebarWidth:100},{...delta,sidebarResizing:undefined}])
    assert.throws(()=>parseSnapshotUpdate(JSON.stringify(bad),previous));
  assert.throws(()=>parseSnapshotUpdate(JSON.stringify(delta)));
});
test('native sidebar menu choices are a closed, validated action vocabulary', () => {
  assert.deepEqual(parseSidebarMenuChoice(JSON.stringify({ action: 'rename', targetId: '' })), { action: 'rename', targetId: '' });
  assert.deepEqual(parseSidebarMenuChoice(JSON.stringify({ action: 'folder', targetId: tab })), { action: 'folder', targetId: tab });
  assert.deepEqual(parseSidebarMenuChoice(JSON.stringify({ action: 'editTheme', targetId: '' })), { action: 'editTheme', targetId: '' });
  for (const choice of [null, { action: 'eval', targetId: '' }, { action: 'rename', targetId: 'unexpected' },
    { action: 'editTheme', targetId: tab }, { action: 'folder', targetId: '' }, { action: 'folder', targetId: '-'.repeat(36) }, { action: 'cancel', targetId: '', extra: true }, { action: 'close' }])
    assert.throws(() => parseSidebarMenuChoice(JSON.stringify(choice)));
});
const fixture = () => ({ revision: 4, canGoBack: false, canGoForward: false, probeMode: false,
  workspaceEnabled: true, schemaVersion: 2, treeRevision: 3, ready: true, error: '', activeId: tab,
  sidebarVisible: true, sidebarWidth: 256, contentBounds: { x: 264, y: 36, width: 912, height: 736 },
  url: 'https://example.com/', isLoading: false, nodes: [
    { id: folder, parentId: '', kind: 'folder', title: 'Work', url: '', position: 0, pinned: true, expanded: false, live: false, favorite: false, archived: false, currentUrl: '', closing: false },
    { id: tab, parentId: folder, kind: 'tab', title: 'Tab', url: 'https://example.com/', position: 0, pinned: true, expanded: true, live: true, favorite: false, archived: false, currentUrl: 'https://example.com/current', closing: false },
  ] });
test('detached inspector retains placement controls and validates device geometry', () => {
  const bounds = { x: 264, y: 36, width: 912, height: 28 };
  const devTools = { id: 5, side: 'window', extent: 0, header: bounds, divider: { x: 0, y: 0, width: 0, height: 0 } };
  assert.equal(parseSnapshot(JSON.stringify({ ...fixture(), devTools })).devTools.side, 'window');
  assert.throws(() => parseSnapshot(JSON.stringify({ ...fixture(), devTools: { ...devTools, side: 'arbitrary' } })));
  const devicePreview = { id: 5, enabled: true, width: 50, height: 9999, dpr: 3, scale: 2, effectiveScale: .05, mobile: true, touch: true, pending: false, error: '', toolbar: bounds, viewport: bounds, area: bounds };
  assert.equal(parseSnapshot(JSON.stringify({ ...fixture(), devicePreview })).devicePreview.height, 9999);
  for (const bad of [{ width: 49 }, { height: 10000 }, { width: 50.5 }, { scale: 2.1 }, { effectiveScale: 0 }, { touch: 1 }])
    assert.throws(() => parseSnapshot(JSON.stringify({ ...fixture(), devicePreview: { ...devicePreview, ...bad } })));
});
test('a ready update is a bounded version string', () => {
  assert.equal(parseSnapshot(JSON.stringify({ ...fixture(), updateReady: '0.1.1' })).updateReady, '0.1.1');
  assert.equal(parseSnapshot(JSON.stringify({ ...fixture(), updateReady: '1.0.0-beta.2' })).updateReady, '1.0.0-beta.2');
  for (const bad of [1, '', 'v0.1.1', '0.1', '0.1.1 <b>', '1.0.0-' + 'x'.repeat(40)])
    assert.throws(() => parseSnapshot(JSON.stringify({ ...fixture(), updateReady: bad })));
});
test('accepts native tree with collapsed parent and live active descendant', () => assert.equal(parseSnapshot(JSON.stringify(fixture())).activeId, tab));
test('Peek has a transient identity, bounded geometry and explicit lifecycle flags', () => {
  const peek = { id:tab, url:'https://example.com/preview', title:'Preview',
    bounds:{x:300,y:70,width:700,height:600}, area:{x:264,y:31,width:912,height:736},
    loading:true, closing:false, crashed:false, canGoBack:false, canGoForward:false };
  assert.equal(parseSnapshot(JSON.stringify({...fixture(),linkPeek:peek})).linkPeek.id,tab);
  for (const bad of [{...peek,id:'bad'},{...peek,closing:1},{...peek,url:'x'.repeat(4097)},
    {...peek,bounds:{...peek.bounds,width:-1}}, {...peek,crashed:undefined}])
    assert.throws(() => parseSnapshot(JSON.stringify({...fixture(),linkPeek:bad})));
  assert.equal(parseSnapshot(JSON.stringify({...fixture(),linkPeek:{...peek,composed:true,opening:false,returning:true,canSplit:false}})).linkPeek.returning,true);
  for (const field of ['composed','opening','returning','canSplit'])
    assert.throws(() => parseSnapshot(JSON.stringify({...fixture(),linkPeek:{...peek,[field]:1}})));
});
const download = () => ({ id: 3, name: 'report.pdf', origin: 'example.com', state: 'in_progress', received: 3221225472, total: 4294967296, speed: 1048576 });
test('accepts downloads with large byte counts and unknown totals', () => {
  const value = { ...fixture(), downloadsStarted: 2, downloads: [download(), { ...download(), id: 4, state: 'complete', total: -1 }] };
  assert.equal(parseSnapshot(JSON.stringify(value)).downloads[0].received, 3221225472);
});
for (const [name, change] of [
  ['empty name', d => { d.name = ''; }],
  ['unknown download state', d => { d.state = 'running'; }],
  ['negative received bytes', d => { d.received = -5; }],
  ['fractional speed', d => { d.speed = 1.5; }],
  ['oversized name', d => { d.name = 'x'.repeat(261); }],
]) test(`rejects download with ${name}`, () => { const d = download(); change(d); assert.throws(() => parseSnapshot(JSON.stringify({ ...fixture(), downloads: [d] }))); });
test('rejects duplicate download IDs and more than thirty entries', () => {
  assert.throws(() => parseSnapshot(JSON.stringify({ ...fixture(), downloads: [download(), download()] })));
  assert.throws(() => parseSnapshot(JSON.stringify({ ...fixture(), downloads: Array.from({ length: 31 }, (_, id) => ({ ...download(), id })) })));
});
test('split navigation state belongs to each pane and requires boolean flags', () => {
  const pane = id => ({ id, bounds: { x: 10, y: 40, width: 400, height: 500 }, canGoBack: false, canGoForward: false, loading: false, borderColor:'#aabbcc' });
  const splitView = { anchor:tab, other:folder, focused:tab, expanded:'', vertical:false, reversed:false, resizing:false, ratio:5000,
    panes:[{ ...pane(tab), canGoBack:true }, { ...pane(folder), loading:true }] };
  const value = { ...fixture(), splitView };
  const parsed = parseSnapshot(JSON.stringify(value)).splitView;
  assert.equal(parsed.panes[0].canGoBack,true);
  assert.equal(parsed.panes[1].canGoBack,false);
  assert.equal(parsed.panes[0].loading,false);
  assert.equal(parsed.panes[1].loading,true);
  for (const key of ['canGoBack','canGoForward','loading']) {
    const malformed = structuredClone(value); malformed.splitView.panes[1][key] = 'false';
    assert.throws(() => parseSnapshot(JSON.stringify(malformed)));
  }
  const malformed = structuredClone(value); malformed.splitView.panes[0].borderColor = 'url(https://example.com/)';
  assert.throws(() => parseSnapshot(JSON.stringify(malformed)));
});

test('accepts live page title and cached favicon address', () => {
  const value = fixture(); value.nodes[1].pageTitle = 'Example Domain'; value.nodes[1].favicon = 'sol://ui/favicon/example.com/v3';
  assert.equal(parseSnapshot(JSON.stringify(value)).nodes[1].pageTitle, 'Example Domain');
});
test('accepts native notice and unavailable shell extension check', () => {
  const value = { ...fixture(), notice: 'An extension tried to open a page Sol does not support, so it was blocked.', noticeId: 2, shellExtensions: -1 };
  assert.equal(parseSnapshot(JSON.stringify(value)).noticeId, 2);
});
for (const [name, change] of [
  ['unknown schema', s => { s.schemaVersion = 999; }],
  ['invalid crash state', s => { s.activeCrashed = 'failed'; }],
  ['invalid window close state', s => { s.windowClosing = 1; }],
  ['invalid node crash state', s => { s.nodes[1].crashed = 'yes'; }],
  ['invalid lifecycle mode', s => { s.lifecycleMode = ['full']; }],
  ['invalid lifecycle counter', s => { s.windowCloseCancelled = -1; }],
  ['invalid address focus request', s => { s.addressRequest = -1; }],
  ['invalid popup count', s => { s.popupCount = 'one'; }],
  ['untyped sidebar visibility', s => { s.sidebarVisible = 'hidden'; }],
  ['invalid content bounds', s => { s.contentBounds.width = -1; }],
  ['invalid sidebar width', s => { s.sidebarWidth = 999; }],
  ['fractional sidebar width', s => { s.sidebarWidth = 256.5; }],
  ['invalid custom title', s => { s.nodes[1].customTitle = 'true'; }],
  ['cycle', s => { s.nodes[0].parentId = folder; }],
  ['unknown parent', s => { s.nodes[1].parentId = 'foreign'; }],
  ['tab used as parent', s => { s.nodes[0].parentId = tab; }],
  ['duplicate node', s => { s.nodes.push({ ...s.nodes[1] }); }],
  ['folder active', s => { s.activeId = folder; }],
  ['fractional revision', s => { s.revision = 1.2; }],
  ['invalid flag', s => { s.nodes[1].pinned = 1; }],
  ['array kind', s => { s.nodes[1].kind = ['tab']; }],
  ['Today inside folder', s => { s.nodes[1].pinned = false; }],
  ['folder favorite', s => { s.nodes[0].favorite = true; }],
  ['archived active tab', s => { s.nodes[1].archived = true; }],
  ['untyped current URL', s => { s.nodes[1].currentUrl = 3; }],
  ['notice without id', s => { s.notice = 'x'; }],
  ['oversized notice', s => { s.notice = 'x'.repeat(241); s.noticeId = 1; }],
  ['invalid shell extension count', s => { s.shellExtensions = -2; }],
  ['untyped window state', s => { s.windowMaximized = 'yes'; }],
  ['negative archive time', s => { s.nodes[1].archivedAt = -1; }],
  ['fractional archive time', s => { s.nodes[1].archivedAt = 1.5; }],
  ['string camera state', s => { s.nodes[1].mediaCamera = 'yes'; }],
  ['numeric microphone state', s => { s.nodes[1].mediaMicrophone = 1; }],
  ['remote favicon', s => { s.nodes[1].favicon = 'https://tracker.example/icon.png'; }],
  ['script favicon', s => { s.nodes[1].favicon = 'data:image/svg+xml;base64,PHN2Zz4='; }],
  ['inline PNG favicon (now served by address)', s => { s.nodes[1].favicon = 'data:image/png;base64,iVBORw0KGgo='; }],
  ['favicon path traversal', s => { s.nodes[1].favicon = 'sol://ui/favicon/../v1'; }],
  ['oversized page title', s => { s.nodes[1].pageTitle = 'x'.repeat(241); }],
]) test(`rejects ${name}`, () => { const value = fixture(); change(value); assert.throws(() => parseSnapshot(JSON.stringify(value))); });
test('accepts a native theme and rejects out-of-range or unknown values', () => {
  const theme = { hue: 145, intensity: 120, mode: 'system', noise: 35, dark: true };
  assert.deepEqual(parseSnapshot(JSON.stringify({ ...fixture(), theme })).theme, theme);
  for (const bad of [{ ...theme, hue: 360 }, { ...theme, hue: -1 }, { ...theme, intensity: 161 }, { ...theme, intensity: 1.5 },
    { ...theme, mode: 'sepia' }, { ...theme, noise: 101 }, { ...theme, dark: 'yes' }, null, 'purple'])
    assert.throws(() => parseSnapshot(JSON.stringify({ ...fixture(), theme: bad })));
});
test('accepts spaces and rejects invalid ones or an unknown active space', () => {
  const work = '33333333-3333-4333-8333-333333333333';
  const spaces = [{ id: work, name: 'Work', icon: '💼', position: 0, hue: 30, intensity: 110 }];
  const ok = parseSnapshot(JSON.stringify({ ...fixture(), spaces, activeSpace: work }));
  assert.equal(ok.activeSpace, work);
  for (const bad of [[], [{ ...spaces[0], name: ' ' }], [{ ...spaces[0], hue: 400 }], [{ ...spaces[0], id: 'short' }], [spaces[0], spaces[0]]])
    assert.throws(() => parseSnapshot(JSON.stringify({ ...fixture(), spaces: bad, activeSpace: work })));
  assert.throws(() => parseSnapshot(JSON.stringify({ ...fixture(), spaces, activeSpace: tab })));
  const nodes = fixture().nodes.map(n => ({ ...n, space: 'not-a-space-id' }));
  assert.throws(() => parseSnapshot(JSON.stringify({ ...fixture(), nodes })));
});
