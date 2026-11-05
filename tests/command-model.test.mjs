import test from 'node:test';
import assert from 'node:assert/strict';
import { commandRows, inlineAddressCompletion, parseHistoryReply } from '../apps/browser-ui/src/command-model.ts';
const tab = (id, extra = {}) => ({ id, kind: 'tab', title: id, url: `https://${id}.example/`, currentUrl: `https://${id}.example/`, position: 0, live: true, ...extra });
const snapshot = { activeId: 'current', nodes: [tab('current', { pageTitle: 'Current title' }), tab('music', { pageTitle: 'My music' }), tab('saved', { live: false, pinned: true }), tab('gone', { archived: true }), tab('closing', { closing: true }), tab('folder', { kind: 'folder' })] };
test('address mode shows the real current site without a redundant reload destination', () => {
  const rows = commandRows(snapshot, 'https://current.example/', 'navigate', 'https://current.example/');
  assert.equal(rows[0].kind, 'current'); assert.equal(rows[0].title, 'Current title');
  assert.deepEqual(rows[0].action, { method: 'command.submit', url: 'https://current.example/' });
  assert.equal(rows.filter(r => r.action.id === 'current').length, 0);
});
test('new mode has no search request until text is entered; filters unavailable entries', () => {
  const rows = commandRows(snapshot, '', 'new', 'https://current.example/');
  assert.deepEqual(rows.map(r => r.key), ['current', 'music', 'saved']);
  assert.ok(rows.every(r => r.action.method === 'command.activate'));
});
test('web search preserves Unicode and special characters', () => {
  const row = commandRows(snapshot, 'привет & мир', 'new', '')[0];
  assert.equal(row.kind, 'search'); assert.equal(row.action.url, 'https://www.google.com/search?q=' + encodeURIComponent('привет & мир'));
});
test('a confident saved host prefix switches by native identity before offering search', () => {
  const rows = commandRows(snapshot, 'music', 'new', '');
  assert.deepEqual(rows[0].action, { method: 'command.activate', id: 'music' }); assert.equal(rows[1].kind, 'search');
});

test('a trailing space explicitly searches instead of promoting catalog, tab or history matches', () => {
  const s = { nodes:[tab('store',{ currentUrl:'https://chromewebstore.google.com/' })] };
  const history = [{ url:'https://chromium.org/',title:'Chromium' }];
  for (const query of ['chrome ','youtu ','chrome\u00a0']) {
    const rows=commandRows(s,query,'new','',history,['chrome download']);
    assert.equal(rows[0].kind,'search');
    assert.equal(rows[0].action.url,'https://www.google.com/search?q='+encodeURIComponent(query.trim()));
  }
  assert.equal(commandRows(s,'chrome','new','')[0].action.id,'store');
  assert.ok(commandRows(s,'chrome ','new','').some(row => row.action.id==='store'));
});

test('inline completion preserves the literal prefix and safely resolves saved-tab addresses', () => {
  const s={ nodes:[tab('store',{ currentUrl:'https://chromewebstore.google.com/' })] };
  const rows=commandRows(s,'Chro','new','');
  assert.equal(inlineAddressCompletion(s,'Chro',rows[0]),'Chromewebstore.google.com');
  const youtube=commandRows({},'youtu','new','');
  assert.equal(inlineAddressCompletion({},'youtu',youtube[0]),'youtube.com');
  assert.equal(inlineAddressCompletion(s,'chrome ',commandRows(s,'chrome ','new','')[0]),undefined);
  assert.equal(inlineAddressCompletion({},'yt',commandRows({},'yt','new','')[0]),undefined);
  assert.equal(inlineAddressCompletion({},'exa',{ kind:'visit',action:{ method:'command.submit',url:'http://example.com/' } }),undefined);
  for (const url of ['javascript:alert(1)','file:///C:/a.pdf','https://user:pass@example.com/']) {
    assert.equal(inlineAddressCompletion({},'exa',{ kind:'visit',action:{ method:'command.submit',url } }),undefined);
  }
});
test('URL entry and saved lazy tab are different actions', () => {
  assert.equal(commandRows(snapshot, 'localhost:8042/a', 'new', '')[0].action.url, 'http://localhost:8042/a');
  assert.equal(commandRows(snapshot, 'saved', 'new', '')[0].live, false);
});
for (const [input, url] of [
  ['youtu', 'https://youtube.com/'], ['YouTu', 'https://youtube.com/'], ['yt', 'https://youtube.com/'],
  ['ютуб', 'https://youtube.com/'], ['ютю', 'https://youtube.com/'], ['ｙｏｕｔｕ', 'https://youtube.com/'],
  ['git', 'https://github.com/'], ['gh', 'https://github.com/'], ['гитх', 'https://github.com/'],
  ['gmai', 'https://mail.google.com/'], ['wiki', 'https://wikipedia.org/'], ['тви', 'https://twitch.tv/'],
]) test(`predicts the site for ${input} with search one arrow away`, () => {
  const rows = commandRows(snapshot, input, 'new', '');
  assert.deepEqual(rows[0].action, { method: 'command.submit', url });
  assert.equal(rows[1].kind, 'search');
});
for (const input of ['y', 'yo', 'g', 'go', 'youtube tutorial', 'github actions', 'wiki meaning', 'ютуб как настроить звук'])
  test(`keeps ${input} as a search rather than guessing navigation`, () => {
    assert.equal(commandRows(snapshot, input, 'new', '')[0].kind, 'search');
  });
test('misspellings are offered without silently replacing the search intent', () => {
  const rows = commandRows(snapshot, 'yotube', 'new', '');
  assert.equal(rows[0].kind, 'search'); assert.equal(rows[1].action.url, 'https://youtube.com/');
});
test('an already open canonical site outranks its catalog suggestion, including brand aliases', () => {
  const s = { nodes: [tab('video', { currentUrl: 'https://www.youtube.com/watch?v=owned', pageTitle: 'A video' })] };
  for (const input of ['youtu', 'yt', 'ютуб']) {
    const rows = commandRows(s, input, 'new', '');
    assert.deepEqual(rows[0].action, { method: 'command.activate', id: 'video' });
    assert.equal(rows[1].kind, 'search');
    assert.equal(rows.filter(r => r.action.url === 'https://youtube.com/').length, 0);
  }
});
test('familiar titles, lookalike hosts and nonstandard ports do not impersonate a catalog origin', () => {
  const s = { nodes: [tab('fake', { currentUrl: 'https://youtube.com.evil.example/', pageTitle: 'YouTube' }),
    tab('port', { currentUrl: 'https://youtube.com:8443/', pageTitle: 'YouTube' })] };
  assert.equal(commandRows(s, 'youtu', 'new', '')[0].action.url, 'https://youtube.com/');
});
test('ambiguous personal host prefixes keep search first', () => {
  const s = { nodes: [tab('app-one'), tab('app-two', { favorite: true })] };
  const rows = commandRows(s, 'app', 'new', '');
  assert.equal(rows[0].kind, 'search'); assert.equal(rows.filter(r => r.kind === 'tab').length, 2);
});
test('an explicit URL always wins over brand prediction or an existing tab', () => {
  const s = { nodes: [tab('video', { currentUrl: 'https://youtube.com/' })] };
  for (const input of ['youtube.com/watch?v=1', 'https://youtube.com/', 'https://youtu/'])
    assert.deepEqual(commandRows(s, input, 'new', '')[0].action, { method: 'command.submit', url: new URL(input.startsWith('https:') ? input : 'https://' + input).href });
});
test('matching only a page title does not take priority over search', () => {
  const s = { nodes: [tab('article', { pageTitle: 'Music for work', currentUrl: 'https://example.org/article' })] };
  const rows = commandRows(s, 'music', 'new', '');
  assert.equal(rows[0].kind, 'search'); assert.equal(rows[1].action.id, 'article');
});
test('an archived or closing official tab does not replace the site destination', () => {
  const s = { nodes: [tab('old', { currentUrl: 'https://youtube.com/', archived: true }), tab('close', { currentUrl: 'https://youtube.com/', closing: true })] };
  assert.equal(commandRows(s, 'yt', 'new', '')[0].action.url, 'https://youtube.com/');
});
test('saved-host prediction works for personal sites outside the catalog', () => {
  const s = { nodes: [tab('work', { title: 'Work', currentUrl: 'https://dashboard.internal.example/tasks', pinned: true, live: false })] };
  assert.deepEqual(commandRows(s, 'dash', 'new', '')[0].action, { method: 'command.activate', id: 'work' });
});
test('search remains available even when many tabs match', () => {
  const s = { nodes: Array.from({ length: 20 }, (_, i) => tab('video' + i, { currentUrl: 'https://youtube.com/watch?v=' + i })) };
  const rows = commandRows(s, 'youtu', 'new', '');
  assert.equal(rows.length, 6); assert.equal(rows[1].kind, 'search'); assert.equal(rows[0].kind, 'tab');
});
test('privileged destinations never become navigation rows', () => {
  for (const input of ['javascript:alert(1)', 'sol://ui/index.html', 'file://server/x', 'https://user:pass@example.com'])
    assert.equal(commandRows(snapshot, input, 'new', '').length, 0);
});
test('a typed local file path becomes one open-file row', () => {
  const rows = commandRows(snapshot, 'file:///C:/docs/a.pdf', 'new', '');
  assert.equal(rows.length, 1); assert.equal(rows[0].action.url, 'file:///C:/docs/a.pdf'); assert.equal(rows[0].detail, 'Open file');
});
test('result count is bounded and matching requires every term', () => {
  assert.equal(commandRows({ nodes: Array.from({ length: 30 }, (_, i) => tab('x' + i)) }, '', 'new', '').length, 6);
  assert.equal(commandRows(snapshot, 'music missing', 'new', '').length, 1);
});
test('visited pages follow tabs, skip duplicates, and a visited host can lead', () => {
  const history = [{ url: 'https://music.example/', title: 'Duplicate of a tab' }, { url: 'https://docs.visited.test/guide', title: 'Guide' },
    { url: 'https://visitedsite.test/', title: 'Visited site' }];
  const rows = commandRows(snapshot, 'guide', 'new', '', history);
  assert.ok(rows.some(r => r.kind === 'history' && r.title === 'Guide'));
  assert.ok(!rows.some(r => r.kind === 'history' && r.action.url === 'https://music.example/'));
  assert.equal(rows[0].kind, 'search');  // A title word alone never outranks search.
  const host = commandRows(snapshot, 'visitedsite', 'new', '', history);
  assert.equal(host[0].kind, 'history'); assert.equal(host[0].action.url, 'https://visitedsite.test/');
  assert.equal(host[1].kind, 'search');  // Search stays one arrow away.
  assert.ok(!commandRows(snapshot, 'https://current.example/', 'navigate', 'https://current.example/', history).some(r => r.kind === 'history'));
});
test('history replies are validated', () => {
  assert.deepEqual(parseHistoryReply('{"text":"a","visits":[{"url":"https://a.test/","title":"A"}]}').visits.length, 1);
  for (const raw of ['{"text":"a","visits":[{"url":"javascript:alert(1)","title":"x"}]}', '{"text":1,"visits":[]}',
    '{"text":"a","visits":[{"url":"https://a.test/","title":' + JSON.stringify('x'.repeat(401)) + '}]}'])
    assert.throws(() => parseHistoryReply(raw));
});

test("Sol's own pages: short addresses typed and shown", async () => {
  const { internalPage, shownAddress } = await import('../apps/browser-ui/src/internal-pages.ts');
  assert.equal(shownAddress('sol://ui/history.html'), 'sol://history');
  assert.equal(shownAddress('https://a.test/'), 'https://a.test/');
  assert.equal(internalPage(' SOL://Downloads/ ')?.url, 'sol://ui/downloads.html');
  assert.equal(internalPage('sol://ui'), undefined);
  const rows = commandRows(snapshot, 'sol://history', 'new', '');
  assert.equal(rows[0].title, 'History');
  assert.deepEqual(rows[0].action, { method: 'command.submit', url: 'sol://history' });
  const page = { ...snapshot, activeId: 'h', nodes: [...snapshot.nodes, tab('h', { url: 'sol://ui/history.html', currentUrl: 'sol://ui/history.html', title: 'History' })] };
  const current = commandRows(page, 'sol://history', 'navigate', 'sol://history');
  assert.equal(current[0].kind, 'current'); assert.equal(current[0].detail, 'History');
});
