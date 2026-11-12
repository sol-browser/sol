import test from 'node:test';
import assert from 'node:assert/strict';
import { placement, descendant, ordered, displayTitle, sidebarWidth, favoriteColumns, favoriteColumnLimit, archiveOrder, pinnedAway, sidebarTitle, tabCloseIcon, tabCloseMethod, folderRemovalSummary, sidebarLayoutKey, sameRowNode } from '../apps/browser-ui/src/sidebar-model.ts';
import { sidebarPresentation } from '../apps/browser-ui/src/sidebar-presentation.ts';

const n = (id, parentId, position, kind = 'tab', pinned = false) => ({ id, parentId, position, kind, pinned, archived: false, favorite: false });
const nodes = () => [n('today', '', 0), n('folder', '', 1, 'folder', true), n('sibling', '', 2, 'tab', true), n('nested', 'folder', 0, 'folder', true), n('child', 'nested', 0, 'tab', true)];

const pin = () => ({ ...n('pin', '', 0, 'tab', true), live: true, customTitle: true,
  title: 'My saved name', pageTitle: 'Draft systems · Mirror', url: 'https://example.org/', currentUrl: 'https://example.org/drafts' });
test('navigated pins show the current title without replacing their saved name', () => {
  const data = pin(); assert.equal(pinnedAway(data), true); assert.equal(sidebarTitle(data), 'Draft systems · Mirror');
  assert.equal(data.title, 'My saved name');
  assert.equal(sidebarTitle({ ...data, currentUrl: data.url }), 'My saved name');
  assert.equal(sidebarTitle({ ...data, pageTitle: '' }), 'My saved name');
});
test('away indicator uses the full URL, normalizes equivalent roots and requires a live pin', () => {
  const data = pin();
  for (const currentUrl of ['https://example.org/?q=1', 'https://example.org/#section', 'https://other.org/'])
    assert.equal(pinnedAway({ ...data, currentUrl }), true);
  for (const variant of [{ live: false }, { pinned: false }, { kind: 'folder' }, { currentUrl: '' },
    { currentUrl: 'https://example.org:443' }]) assert.equal(pinnedAway({ ...data, ...variant }), false);
});
test('live pins unload with minus, closed pins archive with x; Favorites have no close action', () => {
  const data = pin(); assert.equal(tabCloseIcon(data), 'minus');
  assert.equal(tabCloseMethod(data), 'tree.close');
  assert.equal(tabCloseIcon({ ...data, live: false }), 'close');
  assert.equal(tabCloseMethod({ ...data, live: false }), 'tree.archive');
  assert.equal(tabCloseIcon({ ...data, pinned: false }), 'close');
  assert.equal(tabCloseIcon({ ...data, pinned: false, live: false }), 'close');
  for (const variant of [{ favorite: true }, { kind: 'folder' }])
    assert.equal(tabCloseIcon({ ...data, ...variant }), undefined);
});

test('folder confirmation counts nested and hidden split tabs, including unloaded pins', () => {
  const tree = [...nodes(), { ...n('hidden', 'nested', 1, 'tab', true), splitHidden: true },
    { ...n('unloaded', 'folder', 1, 'tab', true), live: false }, n('empty', 'folder', 2, 'folder', true)];
  assert.deepEqual(folderRemovalSummary(tree, 'folder'), { id: 'folder', title: undefined, tabs: 3, folders: 2 });
  assert.equal(folderRemovalSummary(tree, 'child'), undefined);
  assert.equal(folderRemovalSummary(tree, 'missing'), undefined);
  assert.deepEqual(folderRemovalSummary(tree, 'empty'), { id: 'empty', title: undefined, tabs: 0, folders: 0 });
});

test('automatic titles apply to Today, pins, Favorites and unloaded entries; manual names win', () => {
  const base = { kind: 'tab', title: 'Saved', pageTitle: 'Website', customTitle: false };
  for (const variant of [{}, { pinned: true }, { favorite: true }, { live: false }, { archived: true }])
    assert.equal(displayTitle({ ...base, ...variant }), 'Website');
  assert.equal(displayTitle({ ...base, customTitle: true }), 'Saved');
  assert.equal(displayTitle({ ...base, pageTitle: '' }), 'Saved');
  assert.equal(displayTitle({ ...base, kind: 'folder' }), 'Saved');
});
test('sidebar limits round DIP sizes and reserve room for the website', () => {
  assert.equal(sidebarWidth(336.4, 1200), 336); assert.equal(sidebarWidth(999, 1200), 480);
  assert.equal(sidebarWidth(-30, 1200), 150); assert.equal(sidebarWidth(480, 700), 300); assert.equal(sidebarWidth(480, 500), 150);
});
test('dropping a Today page into a folder promotes it atomically', () => assert.deepEqual(placement(nodes(), 'today', 'nested', 'inside', 'today'), { method: 'tree.place', id: 'today', parentId: 'nested', position: -1, section: 'pinned' }));
test('dropping a pinned tab in Today clears its parent', () => assert.deepEqual(placement(nodes(), 'child', null, 'inside', 'today'), { method: 'tree.place', id: 'child', parentId: '', position: -1, section: 'today' }));
test('root section is inherited from a row, never from stale drag origin', () => assert.equal(placement(nodes(), 'today', 'sibling', 'before', 'today').section, 'pinned'));
test('same-parent reorder excludes the dragged row from native target index', () => assert.equal(placement(nodes(), 'folder', 'sibling', 'after', 'pinned').position, 2));
test('split reordering uses real slots even when its second member is hidden', () => {
  const source = { ...n('split', '', 0), splitAnchor: 'split' };
  const target = n('target', '', 2);
  assert.equal(placement([source,target], 'split', 'target', 'after', 'today').position, 2);
  assert.equal(placement([source,target], 'target', 'split', 'before', 'today').position, 0);
});
test('moving a split from a folder uses the destination real root position', () => {
  const source = { ...n('split', 'folder', 0, 'tab', true), splitAnchor: 'split' };
  assert.equal(placement([source,n('target','',4)], 'split', 'target', 'after', 'today').position, 5);
});
test('split placement does not offer Favorites conversion', () => {
  const source = { ...n('split', '', 0), splitAnchor: 'split' };
  assert.equal(placement([source], 'split', null, 'inside', 'favorites'), undefined);
});
test('rejects self, descendant, external, archived and folder-to-Today drops', () => {
  for (const args of [['folder','nested','inside','pinned'], ['folder','folder','after','pinned'], ['foreign','nested','inside','pinned'], ['folder',null,'inside','today']]) assert.equal(placement(nodes(), ...args), undefined);
  const data = nodes(); data[0].archived = true; assert.equal(placement(data, 'today', 'folder', 'inside', 'pinned'), undefined);
});
test('manual order is independent of pin rank and input array order', () => {
  const data = nodes().filter(n => !n.parentId).reverse(); assert.deepEqual(ordered(data).map(n => n.id), ['today','folder','sibling']);
});
test('collapsed ancestor can find a deeply selected descendant without expanding', () => {
  assert.equal(descendant(nodes(), 'child', 'folder'), true); assert.equal(descendant(nodes(), 'sibling', 'folder'), false);
});
test('archive lists the most recently archived first; undated entries follow', () => {
  const node = (id, archived, archivedAt) => ({ id, parentId: '', kind: 'tab', title: id, url: 'https://e.com/', position: 0, pinned: false,
    expanded: true, live: false, favorite: false, archived, currentUrl: '', closing: false, archivedAt });
  const order = archiveOrder([node('old', true, 1000), node('legacy-a', true, 0), node('open', false, 0),
    node('newest', true, 3000), node('legacy-b', true, undefined), node('middle', true, 2000)]).map(n => n.id);
  assert.deepEqual(order, ['newest', 'middle', 'old', 'legacy-b', 'legacy-a']);
});

test('only id-addressed tree commands are retried after a revision race', async () => {
  const { retryable, isStaleRevision } = await import('../apps/browser-ui/src/sidebar-model.ts');
  assert.equal(retryable({ method: 'tree.close', id: 'a' }), true);
  assert.equal(retryable({ method: 'tree.rename', id: 'a', title: 'x' }), true);
  assert.equal(retryable({ method: 'tree.removeFolder', id: 'a' }), true);
  assert.equal(retryable({ method: 'tree.removeFolder', id: 'a', confirmation: { revision: 1, tabs: 3 } }), false,
    'a changed workspace must never silently reuse destructive confirmation');
  assert.equal(retryable({ method: 'tree.move', id: 'a', parentId: '', position: 1 }), false);
  assert.equal(retryable({ method: 'tree.place', id: 'a', parentId: '', position: 1, section: 'today' }), false);
  assert.equal(retryable({ method: 'tree.createTab', parentId: '', title: 'x', url: 'https://x/' }), false);
  assert.equal(isStaleRevision(new Error('409: Workspace changed; retry')), true);
  assert.equal(isStaleRevision(new Error('409: Workspace changed; refresh and retry')), true);
  assert.equal(isStaleRevision(new Error('409: Cannot open this link now')), false);
});

test('a closing Today tab leaves the list at once; pinned and favorites stay', async () => {
  const { withoutClosingTabs } = await import('../apps/browser-ui/src/sidebar-model.ts');
  const tree = [{ id: 'a', closing: true }, { id: 'b', closing: true, pinned: true }, { id: 'c', closing: true, favorite: true }, { id: 'd' }];
  assert.deepEqual(withoutClosingTabs(tree).map(n => n.id), ['b', 'c', 'd']);
  const calm = [{ id: 'd' }];
  assert.equal(withoutClosingTabs(calm), calm, 'unchanged list keeps its identity (no extra motion pass)');
});
test('spaces: top-level entries show in their space; Favorites and nested entries everywhere', async () => {
  const { inSpace, spaceGlyph, neighborSpace } = await import('../apps/browser-ui/src/sidebar-model.ts');
  const node = (extra) => ({ id: 'x', parentId: '', kind: 'tab', title: 't', url: 'https://a/', position: 0, pinned: false, expanded: true, live: false, favorite: false, archived: false, currentUrl: '', closing: false, ...extra });
  assert.equal(inSpace(node({ space: 'work' }), 'work'), true);
  assert.equal(inSpace(node({ space: 'home' }), 'work'), false);
  assert.equal(inSpace(node({ favorite: true, pinned: true, space: '' }), 'work'), true);
  assert.equal(inSpace(node({ parentId: 'folder', pinned: true, space: '' }), 'work'), true);
  assert.equal(inSpace(node({ space: 'home' }), undefined), true, 'older snapshots show everything');
  assert.equal(spaceGlyph({ name: 'work', icon: '' }), 'W');
  assert.equal(spaceGlyph({ name: 'Работа', icon: '' }), 'Р');
  assert.equal(spaceGlyph({ name: 'x', icon: '💼' }), '💼');
  const spaces = [{ id: 'b', position: 1 }, { id: 'a', position: 0 }, { id: 'c', position: 2 }];
  assert.equal(neighborSpace(spaces, 'a', 1), 'b');
  assert.equal(neighborSpace(spaces, 'c', 1), undefined);
  assert.equal(neighborSpace(spaces, 'b', -1), 'a');
});


test('both sidebar surfaces receive updates, media and browser requests from the same projection', () => {
  const snapshot={ready:true,nodes:[],treeRevision:12,activeId:'tab',spaces:[],activeSpace:'work',sidebarVisible:false,
    updateReady:'0.1.6',sidebarRequest:{id:9,action:'newFolder'},media:[{id:4,title:'Playing'}],shortcutLabels:{newTab:'Ctrl+T'}};
  const docked=sidebarPresentation(snapshot,'docked'), floating=sidebarPresentation(snapshot,'floating');
  const {hidden:dh,activeSurface:da,...dockData}=docked;
  const {hidden:fh,activeSurface:fa,...floatData}=floating;
  assert.deepEqual(dockData,floatData);
  assert.equal(floating.updateReady,'0.1.6');assert.equal(floating.media,snapshot.media);
  assert.equal(floating.request,snapshot.sidebarRequest);assert.equal(floating.newTabShortcut,'Ctrl+T');
  assert.deepEqual([dh,da,fh,fa],[true,false,false,true]);
  const open={...snapshot,sidebarVisible:true};
  assert.equal(sidebarPresentation(open,'docked').activeSurface,true);
  assert.equal(sidebarPresentation(open,'floating').activeSurface,false);
});
test('closing, standalone and empty sidebar states have identical semantics in both hosts', () => {
  for(const surface of ['docked','floating']) {
    const empty=sidebarPresentation({ready:false},surface);
    assert.equal(empty.busy,true);assert.deepEqual(empty.nodes,[]);assert.deepEqual(empty.spaces,[]);
    const closing=sidebarPresentation({ready:true,windowClosing:true,windowKind:'standalone'},surface);
    assert.equal(closing.busy,true);assert.equal(closing.standalone,true);
  }
});

test('favorites rearrange into fewer columns as the sidebar narrows', () => {
  // Grid width = sidebar width - 20 px padding - 8 px zone margins.
  assert.equal(favoriteColumnLimit(256 - 28), 4); assert.equal(favoriteColumnLimit(200 - 28), 3);
  assert.equal(favoriteColumnLimit(170 - 28), 2); assert.equal(favoriteColumnLimit(150 - 20), 2); assert.equal(favoriteColumnLimit(10), 2);
  assert.deepEqual([1, 2, 4, 5, 6, 8, 12].map(n => favoriteColumns(n)), [1, 2, 4, 3, 3, 4, 4]);
  assert.deepEqual([1, 2, 4, 5, 6, 8, 12].map(n => favoriteColumns(n, 2)), [1, 2, 2, 2, 2, 2, 2]);
  assert.deepEqual([4, 5, 6, 9].map(n => favoriteColumns(n, 3)), [2, 3, 3, 3]);
});

test('moving a live tab to a Space of another profile asks first', async () => {
  const { crossProfileLiveMove } = await import('../apps/browser-ui/src/sidebar-model.ts');
  const spaces = [{ id: 'a', profileId: 'default' }, { id: 'b', profileId: 'default' }, { id: 'w', profileId: 'work' }];
  const node = (id, extra = {}) => ({ id, parentId: '', kind: 'tab', title: id, url: '', position: 0, pinned: false, expanded: true,
    live: false, favorite: false, archived: false, currentUrl: '', closing: false, space: 'a', ...extra });
  const nodes = [node('live', { live: true }), node('cold'), node('folder', { kind: 'folder', pinned: true }),
    node('inside', { parentId: 'folder', pinned: true, live: true, space: undefined }), node('split', { splitAnchor: 'split', splitPanes: ['split', 'partner'] }),
    node('partner', { splitAnchor: 'split', splitPanes: ['split', 'partner'], live: true }), node('peeked', { peek: true })];
  assert.equal(crossProfileLiveMove(nodes, spaces, 'live', 'w'), true);
  assert.equal(crossProfileLiveMove(nodes, spaces, 'live', 'b'), false, 'same profile moves directly');
  assert.equal(crossProfileLiveMove(nodes, spaces, 'cold', 'w'), false, 'cold tabs move directly');
  assert.equal(crossProfileLiveMove(nodes, spaces, 'folder', 'w'), true, 'a live page inside the folder');
  assert.equal(crossProfileLiveMove(nodes, spaces, 'split', 'w'), true, 'the live other pane of a split');
  assert.equal(crossProfileLiveMove(nodes, spaces, 'peeked', 'w'), true, 'a Link Peek');
  assert.equal(crossProfileLiveMove(nodes, spaces, 'missing', 'w'), false);
});

test('the layout key changes only when rows can move', () => {
  const row = (id, extra = {}) => ({ id, parentId: '', kind: 'tab', title: 'A', url: 'https://a/', position: 0, pinned: false, expanded: true,
    live: false, favorite: false, archived: false, currentUrl: 'https://a/', closing: false, ...extra });
  const base = [row('a'), row('b', { position: 1 }), row('f', { kind: 'folder', pinned: true, position: 2 }), row('c', { parentId: 'f', pinned: true })];
  const key = sidebarLayoutKey(base);
  // Nothing moves: a title, icon, address, loading, sound or preview change.
  for (const extra of [{ title: 'B' }, { pageTitle: 'New title' }, { favicon: 'sol://ui/favicon/a/v2' }, { currentUrl: 'https://a/next' },
    { live: true }, { audible: true }, { muted: true }, { peek: true }, { crashed: true }, { mediaCamera: true }])
    assert.equal(sidebarLayoutKey([{ ...base[0], ...extra }, ...base.slice(1)]), key);
  // Rows move: added, removed, reordered, nested, pinned, made a Favorite, folder opened or closed.
  for (const changed of [[...base, row('d', { position: 3 })], base.slice(1), [base[1], base[0], base[2], base[3]],
    [{ ...base[0], position: 5 }, ...base.slice(1)], [{ ...base[0], parentId: 'f', pinned: true }, ...base.slice(1)],
    [{ ...base[0], pinned: true }, ...base.slice(1)], [{ ...base[0], pinned: true, favorite: true }, ...base.slice(1)],
    [base[0], base[1], { ...base[2], expanded: false }, base[3]]])
    assert.notEqual(sidebarLayoutKey(changed), key);
  // A tab's own "expanded" flag is not layout.
  assert.equal(sidebarLayoutKey([{ ...base[0], expanded: false }, ...base.slice(1)]), key);
});

test('a row is the same when only fields it does not show changed', () => {
  const node = { id: 'a', parentId: '', kind: 'tab', title: 'A', url: 'https://a/', position: 4, pinned: false, expanded: true, live: true,
    favorite: false, archived: false, currentUrl: 'https://a/', closing: false, space: 's1', archivedAt: 0, favicon: 'sol://ui/favicon/a/v1' };
  assert.equal(sameRowNode(node, node), true);
  assert.equal(sameRowNode(node, { ...node }), true);
  // Another tab opened above: only the slot moved.
  assert.equal(sameRowNode(node, { ...node, position: 5 }), true);
  assert.equal(sameRowNode(node, { ...node, space: 's2', archivedAt: 7 }), true);
  for (const extra of [{ title: 'B' }, { pageTitle: 'T' }, { favicon: 'sol://ui/favicon/a/v2' }, { live: false }, { closing: true }, { pinned: true },
    { currentUrl: 'https://a/x' }, { audible: true }, { muted: true }, { peek: true }, { expanded: false }, { parentId: 'f' }, { xBadgeHidden: true }])
    assert.equal(sameRowNode(node, { ...node, ...extra }), false, JSON.stringify(extra));
  // A field that disappears or appears is a change, also when the count stays equal.
  const { favicon, ...without } = node;
  assert.equal(sameRowNode(node, without), false);
  assert.equal(sameRowNode(node, { ...without, peekTitle: 'x' }), false);
  assert.equal(sameRowNode(node, undefined), false); assert.equal(sameRowNode(undefined, undefined), true);
});
