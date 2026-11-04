import test from 'node:test';
import assert from 'node:assert/strict';
import { placement, descendant, ordered, displayTitle, sidebarWidth, archiveOrder, pinnedAway, sidebarTitle, tabCloseIcon, tabCloseMethod, folderRemovalSummary } from '../apps/browser-ui/src/sidebar-model.ts';
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
  assert.equal(sidebarWidth(-30, 1200), 200); assert.equal(sidebarWidth(480, 700), 300);
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
