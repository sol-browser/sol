import test from 'node:test';
import assert from 'node:assert/strict';
import { groupBlockerLists, normalizeTrustedSite, parseBlockerState } from '../apps/browser-ui/src/blocker-model.ts';

const state = (patch = {}) => ({ lists: [
  { key: 'easylist', title: 'EasyList', group: 'ads', enabled: true, rules: 40000, updated: 1700000000000 },
  { key: 'ublock-filters', title: 'uBlock filters', group: 'default', enabled: true, rules: 9000, updated: 0 },
  { key: 'odd', title: 'Odd', group: 'weird', enabled: false, rules: -5, updated: 'x' },
], updating: false, rules: 50000, trusted: ['example.com', 7, ''], userFilters: '||ads.test^', waitForLists: true, ...patch });

test('parses native ad blocker state and bounds it', () => {
  const parsed = parseBlockerState(state());
  assert.equal(parsed.lists.length, 3);
  assert.equal(parsed.lists[2].group, 'custom');
  assert.equal(parsed.lists[2].rules, 0);
  assert.deepEqual(parsed.trusted, ['example.com']);
  assert.equal(parsed.waitForLists, true);
  assert.throws(() => parseBlockerState(null));
});

test('groups lists in uBlock Origin order', () => {
  const groups = groupBlockerLists(parseBlockerState(state()).lists).map(([group]) => group);
  assert.deepEqual(groups, ['default', 'ads', 'custom']);
});

test('normalizes typed sites to hostnames', () => {
  assert.equal(normalizeTrustedSite(' Example.com '), 'example.com');
  assert.equal(normalizeTrustedSite('https://www.YouTube.com/watch?v=1'), 'youtube.com');
  assert.equal(normalizeTrustedSite('news.example.co.uk/path'), 'news.example.co.uk');
  assert.equal(normalizeTrustedSite('*.example.com'), null);
  assert.equal(normalizeTrustedSite('a..b'), null);
  assert.equal(normalizeTrustedSite(''), null);
});
