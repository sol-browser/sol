import test from 'node:test';
import assert from 'node:assert/strict';
import { shareEqual, sharedUpdate } from '../apps/browser-ui/src/snapshot-sharing.ts';
import { parseSnapshotUpdate, DEFAULT_THEME } from '../packages/browser-contracts/src/index.ts';

const node = (id, title) => ({ id, parentId: '', kind: 'tab', title, live: true, favicon: `sol://ui/favicon/${id}/v1` });
const snapshot = (revision, nodes, extra = {}) => ({ revision, isLoading: false, nodes, spaces: [{ id: 's', name: 'Home' }],
  theme: { hue: 280, palette: { circles: [{ hue: 1 }] } }, ...extra });

test('a full snapshot that only changes top-level fields keeps every unchanged part', () => {
  const before = snapshot(1, [node('a', 'A'), node('b', 'B')]);
  const parsed = JSON.parse(JSON.stringify(snapshot(2, [node('a', 'A'), node('b', 'B')], { isLoading: true })));
  const next = shareEqual(before, parsed);
  assert.notEqual(next, before);
  assert.equal(next.revision, 2); assert.equal(next.isLoading, true);
  assert.equal(next.nodes, before.nodes);
  assert.equal(next.spaces, before.spaces);
  assert.equal(next.theme, before.theme);
});

test('a changed row gets a new object; the other rows and the theme keep theirs', () => {
  const before = snapshot(1, [node('a', 'A'), node('b', 'B'), node('c', 'C')]);
  const parsed = JSON.parse(JSON.stringify(snapshot(2, [node('a', 'A'), node('b', 'B2'), node('c', 'C')])));
  const next = shareEqual(before, parsed);
  assert.notEqual(next.nodes, before.nodes);
  assert.equal(next.nodes[0], before.nodes[0]); assert.equal(next.nodes[2], before.nodes[2]);
  assert.notEqual(next.nodes[1], before.nodes[1]); assert.equal(next.nodes[1].title, 'B2');
  assert.deepEqual(next, parsed);
});

test('added, removed and reordered entries are never mistaken for unchanged ones', () => {
  const before = snapshot(1, [node('a', 'A'), node('b', 'B')]);
  const added = shareEqual(before, JSON.parse(JSON.stringify(snapshot(2, [node('a', 'A'), node('b', 'B'), node('c', 'C')]))));
  assert.notEqual(added.nodes, before.nodes); assert.equal(added.nodes.length, 3); assert.equal(added.nodes[0], before.nodes[0]);
  const removed = shareEqual(before, JSON.parse(JSON.stringify(snapshot(2, [node('a', 'A')]))));
  assert.notEqual(removed.nodes, before.nodes); assert.equal(removed.nodes.length, 1);
  const swapped = shareEqual(before, JSON.parse(JSON.stringify(snapshot(2, [node('b', 'B'), node('a', 'A')]))));
  assert.deepEqual(swapped.nodes.map(n => n.id), ['b', 'a']);
  // A field that disappears (an optional value cleared) is a change.
  const withZoom = { ...before, zoom: 150 };
  const cleared = shareEqual(withZoom, JSON.parse(JSON.stringify(before)));
  assert.equal('zoom' in cleared, false);
  // A field that appears is a change too, even with an undefined-looking value elsewhere.
  const optional = shareEqual({ a: 1 }, { a: 1, b: undefined });
  assert.equal('b' in optional, true);
});

test('identical input returns the previous object; scalars and type changes pass through', () => {
  const before = snapshot(1, [node('a', 'A')]);
  assert.equal(shareEqual(before, JSON.parse(JSON.stringify(before))), before);
  assert.equal(shareEqual([1, 2], [1, 2]).length, 2);
  assert.deepEqual(shareEqual({ list: [1] }, { list: { 0: 1 } }), { list: { 0: 1 } });
  assert.equal(shareEqual(undefined, 5), 5);
  assert.equal(shareEqual('x', 'x'), 'x');
});

test('the bridge routes only theme deltas to the theme path and shares unchanged parts of full snapshots', () => {
  let previous;
  const decodeUpdate = raw => { const decoded = sharedUpdate(previous, parseSnapshotUpdate(raw, previous)); previous = decoded.snapshot; return decoded; };
  const space = '11111111-1111-4111-8111-111111111111', tab = '22222222-2222-4222-8222-222222222222';
  const full = (revision, extra = {}) => JSON.stringify({ revision, canGoBack: false, canGoForward: false, probeMode: false,
    workspaceEnabled: true, schemaVersion: 2, treeRevision: 3, ready: true, error: '', activeId: tab, activeSpace: space, theme: DEFAULT_THEME,
    sidebarVisible: true, sidebarWidth: 256, contentBounds: { x: 264, y: 36, width: 912, height: 736 }, url: 'https://example.com/', isLoading: false,
    nodes: [{ id: tab, parentId: '', kind: 'tab', title: 'Tab', url: 'https://example.com/', position: 0, pinned: false, expanded: true, live: true,
      favorite: false, archived: false, currentUrl: 'https://example.com/', closing: false }], ...extra });
  const first = decodeUpdate(full(1));
  assert.equal(first.themeOnly, false);
  const loading = decodeUpdate(full(2, { isLoading: true }));
  assert.equal(loading.themeOnly, false); assert.equal(loading.snapshot.nodes, first.snapshot.nodes);
  // Identical content is still a new snapshot object (hosts acknowledge every full snapshot).
  const same = decodeUpdate(full(2, { isLoading: true }));
  assert.notEqual(same.snapshot, loading.snapshot); assert.equal(same.snapshot.nodes, loading.snapshot.nodes);
  const theme = decodeUpdate(JSON.stringify({ themeOnly: true, revision: 3, activeSpace: space, theme: { ...DEFAULT_THEME, hue: 42 } }));
  assert.equal(theme.themeOnly, true); assert.equal(theme.snapshot.theme.hue, 42); assert.equal(theme.snapshot.nodes, first.snapshot.nodes);
  // A full snapshot whose tree is unchanged but whose theme differs is not a theme delta.
  const themed = decodeUpdate(full(4, { isLoading: true, theme: { ...DEFAULT_THEME, hue: 90 } }));
  assert.equal(themed.themeOnly, false); assert.equal(themed.snapshot.theme.hue, 90); assert.equal(themed.snapshot.nodes, first.snapshot.nodes);
  const layout = decodeUpdate(JSON.stringify({ layoutOnly: true, revision: 5, sidebarWidth: 300, sidebarResizing: true }));
  assert.equal(layout.themeOnly, false); assert.equal(layout.snapshot.sidebarWidth, 300); assert.equal(layout.snapshot.theme, themed.snapshot.theme);
});
