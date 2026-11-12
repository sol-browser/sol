import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { parseSnapshotUpdate } from '../packages/browser-contracts/src/index.ts';
import { dividerKey, dividerLabel, dividerStep, splitLanding, splitMembers, splitNamedMember, splitRowCompact, SPLIT_SEGMENT_MIN_WIDTH } from '../apps/browser-ui/src/split-model.ts';

const source = path => readFileSync(new URL(`../${path}`, import.meta.url), 'utf8');
const node = (id, extra = {}) => ({ id, parentId: '', kind: 'tab', title: id, url: '', position: 0, pinned: false, expanded: true,
  live: true, favorite: false, archived: false, currentUrl: '', closing: false, ...extra });

test('dividers have stable keys and keyboard steps along their own axis', () => {
  assert.equal(dividerKey({ path: [], index: 0, cross: false }), 'd:0');
  assert.equal(dividerKey({ path: [1, 0], index: 2, cross: false }), 'd1.0:2');
  assert.notEqual(dividerKey({ path: [], index: 0, cross: true }), dividerKey({ path: [], index: 0, cross: false }));
  assert.equal(dividerStep('h', 'ArrowLeft'), -1); assert.equal(dividerStep('h', 'ArrowRight'), 1);
  assert.equal(dividerStep('h', 'ArrowUp'), undefined, 'a vertical line ignores up/down');
  assert.equal(dividerStep('v', 'ArrowUp'), -1); assert.equal(dividerStep('v', 'ArrowDown'), 1);
  assert.equal(dividerStep('v', 'Home'), 0); assert.equal(dividerStep('v', 'Enter'), undefined);
  assert.match(dividerLabel({ axis: 'h', cross: false }), /side by side/u);
  assert.match(dividerLabel({ axis: 'h', cross: true }), /grid/u);
});

test('a split row lists its pages in on-screen order and names the focused one', () => {
  const panes = ['c', 'a', 'd', 'b'];
  const anchor = node('a', { splitAnchor: 'a', splitPanes: panes });
  const tree = [anchor, ...['b', 'c', 'd'].map(id => node(id, { splitAnchor: 'a', splitPanes: panes, splitHidden: true }))];
  assert.deepEqual(splitMembers(anchor, tree).map(n => n.id), panes);
  assert.equal(splitMembers(anchor, tree)[1], anchor, 'the anchor keeps its own identity');
  assert.deepEqual(splitMembers(tree[1], tree).map(n => n.id), ['b'], 'only the anchor row is a group');
  assert.deepEqual(splitMembers({ ...anchor, splitPanes: ['a', 'missing'] }, tree).map(n => n.id), ['a'], 'a missing partner never shows a broken pair');
  assert.equal(splitNamedMember(anchor, 'd'), 'd', 'the pane in focus keeps its name');
  assert.equal(splitNamedMember(anchor, 'elsewhere'), 'a', 'off screen, the row names its own page');
  assert.equal(splitNamedMember(anchor, 'd', 'b'), 'b', 'the tab being renamed shows its field');
  assert.equal(splitNamedMember(anchor, 'd', 'elsewhere'), 'd');
  assert.match(source('apps/browser-ui/src/split-view.css'), /\.split-row-content:has\(\.split-row-edit\) > \.split-row-part:not\(:has\(\.split-row-edit\)\)/u,
    'while renaming, the other segments shrink to icons at any width');
});

test('compact split rows follow the documented width per segment, mirrored by CSS container queries', () => {
  // A segment needs its icon, a close button and a readable part of its name.
  assert.equal(SPLIT_SEGMENT_MIN_WIDTH, 100);
  assert.equal(splitRowCompact(2, 220), false, 'two names fit a default sidebar');
  assert.equal(splitRowCompact(3, 220), true); assert.equal(splitRowCompact(4, 220), true);
  assert.equal(splitRowCompact(3, 320), false, 'a wide sidebar shows every name');
  const css = source('apps/browser-ui/src/split-view.css');
  for (const panes of [2, 3, 4]) {
    const limit = panes * SPLIT_SEGMENT_MIN_WIDTH - 1;
    assert.match(css, new RegExp(`@container split-row \\(max-width:${limit}px\\)[^@]*data-panes="${panes}"`, 'u'), `${panes} panes collapse below ${limit + 1}px`);
  }
});

test('every Split View method the shell sends is on the trusted native allowlist, and only those', () => {
  const ui = ['SplitView.tsx', 'Sidebar.tsx', 'WorkspaceSidebar.tsx', 'main.tsx'].map(file => source(`apps/browser-ui/src/${file}`)).join('\n');
  const sent = new Set([...ui.matchAll(/method: ?'(split\.[A-Za-z]+)'/gu)].map(match => match[1]));
  const policy = source('apps/desktop/src/trust_policy.h');
  const allowed = new Set([...policy.matchAll(/method == "(split\.[A-Za-z]+)"/gu)].map(match => match[1]));
  for (const method of ['split.add', 'split.menu', 'split.nudge', 'split.beginResize', 'split.beginMove', 'split.remove', 'split.swap']) assert.ok(sent.has(method), `${method} is used`);
  // The divider hover probe is a read-only query answered by the shell's own route.
  assert.match(source('apps/desktop/src/main_win.cpp'), /method == "split\.resizeHovered"/u);
  allowed.add('split.resizeHovered');
  for (const method of sent) assert.ok(allowed.has(method), `${method} must be allowed natively`);
  assert.ok(!allowed.has('split.layout') && !allowed.has('split.preview'), 'retired two-pane methods stay refused');
  // Divider movement and layout changes are native: the shell never sends a layout tree or a ratio.
  assert.doesNotMatch(source('apps/browser-ui/src/SplitView.tsx'), /ratio|layout:/u);
});

test('both sidebar hosts get Split View rows only through WorkspaceSidebar', () => {
  const shared = source('apps/browser-ui/src/WorkspaceSidebar.tsx');
  assert.match(shared, /tabCommand: command/u);
  assert.match(shared, /satisfies SidebarProps & Record<keyof SidebarProps, unknown>/u);
  for (const host of ['main.tsx', 'Peek.tsx']) {
    const text = source(`apps/browser-ui/src/${host}`);
    assert.match(text, /<WorkspaceSidebar /u, `${host} uses the shared binding`);
    assert.doesNotMatch(text, /<(Stable)?Sidebar /u, `${host} binds no Sidebar props itself`);
  }
});

test('a light layout update carries the split geometry and clears it when the split is gone', () => {
  const id = n => `${n}${n}${n}${n}${n}${n}${n}${n}-${n}${n}${n}${n}-4${n}${n}${n}-8${n}${n}${n}-${n.repeat(12)}`;
  const pane = (n, x) => ({ id: id(n), bounds: { x, y: 40, width: 200, height: 300 }, canGoBack: false, canGoForward: false, loading: false, borderColor: '#aabbcc' });
  const splitView = { anchor: id('1'), focused: id('1'), expanded: '', resizing: true, linked: false, layout: 'h(6000*0,4000*1)', canAdd: true,
    presets: ['columns', 'rows'], panes: [pane('1', 0), pane('2', 250)], dividers: [{ path: [], index: 0, axis: 'h', cross: false, share: 60, bounds: { x: 200, y: 40, width: 8, height: 300 } }] };
  const previous = { revision: 3, nodes: [{ id: id('1') }], splitView: { ...splitView, layout: 'h(5000*0,5000*1)' } };
  const next = parseSnapshotUpdate(JSON.stringify({ layoutOnly: true, revision: 4, sidebarResizing: false, splitView }), previous);
  assert.equal(next.splitView.layout, 'h(6000*0,4000*1)');
  assert.equal(next.nodes, previous.nodes, 'the tree is not resent');
  assert.equal(parseSnapshotUpdate(JSON.stringify({ layoutOnly: true, revision: 5, sidebarResizing: false }), next).splitView, undefined);
  assert.throws(() => parseSnapshotUpdate(JSON.stringify({ layoutOnly: true, revision: 6, sidebarResizing: false, splitView: { ...splitView, panes: [] } }), next));
  // A pane carried over the sidebar is one of the split's panes.
  const leaving = parseSnapshotUpdate(JSON.stringify({ layoutOnly: true, revision: 7, sidebarResizing: false, splitView: { ...splitView, leaving: id('2') } }), next);
  assert.equal(leaving.splitView.leaving, id('2'));
  assert.throws(() => parseSnapshotUpdate(JSON.stringify({ layoutOnly: true, revision: 8, sidebarResizing: false, splitView: { ...splitView, leaving: id('3') } }), next));
});

test('a pane dragged to the sidebar shows its row where native code will put it', () => {
  const group = (extra = {}) => [node('a', { splitAnchor: 'a', splitPanes: ['a', 'b'], ...extra }),
    node('b', { splitAnchor: 'a', splitPanes: ['a', 'b'], splitHidden: true, position: 7 })];
  assert.deepEqual(splitLanding(group(), 'b'), { kind: 'below' }, 'a Today group: right below it');
  assert.deepEqual(splitLanding(group({ pinned: true }), 'b'), { kind: 'todayTop' }, 'a pinned group: the top of Today, not under the pin');
  assert.deepEqual(splitLanding(group({ pinned: true, parentId: 'folder' }), 'b'), { kind: 'todayTop' }, 'a group in a folder: the top of Today');
  const pinnedMember = [node('a', { splitAnchor: 'a', splitPanes: ['a', 'b'] }), node('b', { splitAnchor: 'a', splitPanes: ['a', 'b'], splitHidden: true, pinned: true, parentId: 'folder', position: 3 })];
  assert.deepEqual(splitLanding(pinnedMember, 'b'), { kind: 'own', parentId: 'folder', position: 3 }, 'a pinned page returns to its own place');
  assert.deepEqual(splitLanding([...group().slice(0, 1), node('b', { splitAnchor: 'a', splitPanes: ['a', 'b'], favorite: true, pinned: true })], 'b'), { kind: 'none' });
  assert.deepEqual(splitLanding(group(), 'missing'), { kind: 'none' });
});
