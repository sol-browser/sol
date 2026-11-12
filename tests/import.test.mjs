import test from 'node:test';
import assert from 'node:assert/strict';
import { parseSources, parseProfile, parsePreview, parseReport, previewLines, reportLines, defaultTypes, DATA_TYPES } from '../apps/browser-ui/src/import-model.ts';

test('sources keep known browsers and numeric profile ids only', () => {
  const sources = parseSources(JSON.stringify({ sources: [
    { id: 'chrome', name: 'Chrome', profiles: [{ id: '0', name: 'Default profile' }, { id: '../x', name: 'bad' }] },
    { id: 'opera', name: 'Opera', profiles: [] }, { id: 'arc', name: 'Arc', profiles: [] }] }));
  assert.deepEqual(sources.map(s => s.id), ['chrome', 'arc']);
  assert.deepEqual(sources[0].profiles, [{ id: '0', name: 'Default profile' }]);
  assert.throws(() => parseSources('{}'));
});

test('a cancelled picker is not a preview or a profile', () => {
  assert.equal(parsePreview('{}'), null);
  assert.equal(parseProfile('{}'), null);
  assert.deepEqual(parseProfile('{"profile":{"id":"4","name":"Arc"}}'), { id: '4', name: 'Arc' });
});

test('preview counts are sanitised and summarised', () => {
  const p = parsePreview(JSON.stringify({ label: 'Chrome', tabs: 2, bookmarks: 3, spaces: 1, history: 1, cookies: -4,
    passwords: { found: 7, ready: 2, appBound: 1, unreadable: 'x' }, notes: ['n'], needsPrimaryPassword: false }));
  assert.equal(p.cookies, 0); assert.equal(p.passwords.unreadable, 0); assert.equal(p.passwords.appBound, 1);
  assert.deepEqual(previewLines(p), ['1 Space', '2 tabs', '3 bookmarks', '1 history visit', '2 saved passwords']);
});

test('Arc previews and reports retain workspace counts, including an empty Space', () => {
  const data = { label: 'Arc', spaces: 3, tabs: 8, folders: 2, favorites: 2, splits: 1, archived: 2, favicons: 3 };
  assert.deepEqual(previewLines(parsePreview(JSON.stringify(data))), ['3 Spaces', '8 tabs', '2 folders', '2 Favorites', '1 Split View', '2 archived tabs', '3 site icons']);
  assert.deepEqual(reportLines(parseReport(JSON.stringify(data))), ['3 Spaces added', '8 tabs added', '2 folders added', '2 Favorites added', '1 Split View added', '2 archived tabs added', '3 site icons added']);
  assert.deepEqual(previewLines(parsePreview('{"label":"Arc","spaces":1}')), ['1 Space']);
  assert.deepEqual(reportLines(parseReport('{"label":"Arc","spaces":1,"tabs":0}')), ['1 Space added']);
});

test('report lines say what was added and what was kept', () => {
  const r = parseReport(JSON.stringify({ label: 'Zen', tabs: 4, bookmarks: 1, history: 0, cookies: { written: 2, kept: 1, failed: 0 },
    passwords: { added: 2, existing: 1, failed: 0 }, errors: ['e'] }));
  assert.deepEqual(reportLines(r), ['4 tabs added', '1 bookmark added', '0 history visits added', '2 cookies added, 1 already in Sol', '2 passwords saved, 1 already saved']);
  assert.deepEqual(parseReport('{"passwords":{}}').errors, []);
});

test('each browser offers its own data types', () => {
  assert.ok(DATA_TYPES.zen.some(t => t.type === 'cookies'));
  assert.ok(!DATA_TYPES.chrome.some(t => t.type === 'cookies'));
  assert.equal(DATA_TYPES.arc.find(t => t.type === 'tabs')?.label, 'Spaces and sidebar tabs');
  assert.deepEqual(defaultTypes('edge'), { tabs: true, bookmarks: true, history: true, cookies: false, passwords: true });
});
