import test from 'node:test';
import assert from 'node:assert/strict';
import { parseDownloadsPage, groupDownloads, savedStatus, matches } from '../apps/browser-ui/src/downloads-page-model.ts';

const saved = (patch = {}) => ({ id: 1, name: 'report.pdf', origin: 'example.com', state: 'complete', total: 1200, finishedAt: Date.now(), exists: true, ...patch });
const active = (patch = {}) => ({ id: 7, name: 'big.zip', origin: 'example.com', state: 'in_progress', received: 10, total: -1, speed: 5, ...patch });

test('parses native downloads page replies', () => {
  const page = parseDownloadsPage(JSON.stringify({ active: [active()], items: [saved()], more: false }));
  assert.equal(page.active.length, 1); assert.equal(page.items[0].name, 'report.pdf');
});

test('rejects malformed rows and paths', () => {
  for (const [name, value] of [
    ['path field is just extra, but bad state rejects', { active: [], items: [saved({ state: 'in_progress' })], more: false }],
    ['saved id 0', { active: [], items: [saved({ id: 0 })], more: false }],
    ['fractional size', { active: [], items: [saved({ total: 1.5 })], more: false }],
    ['active completed', { active: [active({ state: 'complete' })], items: [], more: false }],
    ['missing exists', { active: [], items: [{ ...saved(), exists: undefined }], more: false }],
    ['more not boolean', { active: [], items: [], more: 1 }],
  ]) assert.throws(() => parseDownloadsPage(JSON.stringify(value)), undefined, name);
});

test('status, grouping and search', () => {
  assert.equal(savedStatus(saved({ exists: false })), 'Deleted or moved');
  assert.equal(savedStatus(saved({ state: 'canceled', exists: false })), 'Canceled');
  assert.equal(savedStatus(saved()), '');
  const now = new Date(2026, 9, 22, 15).getTime();
  const groups = groupDownloads([saved({ finishedAt: now }), saved({ id: 2, finishedAt: now - 86_400_000 })], now);
  assert.deepEqual(groups.map(g => g.label), ['Today', 'Yesterday']);
  assert.ok(matches(saved(), 'REPORT') && matches(saved(), 'example') && !matches(saved(), 'zip'));
});
