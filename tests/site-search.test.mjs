import test from 'node:test';
import assert from 'node:assert/strict';
import { defaultSiteSearches, validSiteSearches, validSiteSearch, parseSnapshot, parseSettingsSnapshot } from '../packages/browser-contracts/src/index.ts';
import { matchingSiteSearch, siteSearchKey, siteSearchTemplate, siteSearchUrl, wikipediaLanguage } from '../apps/browser-ui/src/site-search.ts';
import { readFileSync } from 'node:fs';
const site = { name: 'Owned search', keyword: 'owned', url: 'https://example.test/search?q=%s' };

test('shortcuts require exact typed text; disabled defaults stay disabled', () => {
  for (const input of ['yt', 'YT']) assert.equal(matchingSiteSearch({}, input)?.name, 'YouTube');
  for (const input of ['y', 'yt ', ' yt', 'yt cats', 'youtube.com']) assert.equal(matchingSiteSearch({}, input), undefined);
  assert.equal(matchingSiteSearch({ siteSearches: [] }, 'yt'), undefined);
  assert.equal(matchingSiteSearch({ siteSearches: [site] }, 'owned'), site);
  assert.equal(matchingSiteSearch({ siteSearches: [site] }, 'yt'), undefined);
  assert.equal(matchingSiteSearch({ siteSearches: [{ ...site, enabled: false }] }, 'owned'), undefined);
  assert.equal(matchingSiteSearch({ siteSearches: [{ ...site, keyword: 'blog.example.com' }] }, 'BLOG.EXAMPLE.COM')?.keyword, 'blog.example.com');
});

test('the built-in Wikipedia searches in the language of the query', () => {
  const wiki = defaultSiteSearches.find(s => s.keyword === 'wiki');
  const host = query => new URL(siteSearchUrl(wiki, query)).host;
  assert.equal(host('чёрная дыра'), 'ru.wikipedia.org');
  assert.equal(host('Київ'), 'uk.wikipedia.org');
  assert.equal(host('чорна діра'), 'uk.wikipedia.org');
  assert.equal(host('black hole'), 'en.wikipedia.org');
  assert.equal(host('2026'), 'en.wikipedia.org');
  assert.equal(host('東京'), 'zh.wikipedia.org');
  assert.equal(host('東京タワー'), 'ja.wikipedia.org');
  assert.equal(host('서울'), 'ko.wikipedia.org');
  assert.equal(host('Αθήνα'), 'el.wikipedia.org');
  assert.equal(new URL(siteSearchUrl(wiki, 'чёрная дыра')).searchParams.get('search'), 'чёрная дыра');
  assert.equal(wikipediaLanguage('Мінск ў'), 'uk'); // Ukrainian letters win over Belarusian ў
  assert.equal(wikipediaLanguage('Мурманск ў'), 'be');
});

test('a Wikipedia address the person set is kept as is', () => {
  const german = { name: 'Wikipedia DE', keyword: 'wde', url: 'https://de.wikipedia.org/w/index.php?search=%s' };
  assert.equal(new URL(siteSearchUrl(german, 'чёрная дыра')).host, 'de.wikipedia.org');
  assert.equal(siteSearchTemplate(site, 'чёрная дыра'), site.url);
});

test('keyboard activation can be Space or Tab, or Tab only', () => {
  assert.equal(siteSearchKey({},' '),true); assert.equal(siteSearchKey({},'Tab'),true);
  assert.equal(siteSearchKey({siteSearchActivation:'tab'},' '),false);
  assert.equal(siteSearchKey({siteSearchActivation:'tab'},'Tab'),true);
  for(const key of ['Enter','ArrowRight','Space']) assert.equal(siteSearchKey({},key),false);
});

test('templates encode Unicode, separators and literal placeholders as one query', () => {
  const query = 'кот & a=1#x / %s ? 😺';
  const result = new URL(siteSearchUrl(site, query));
  assert.equal(result.origin, 'https://example.test');
  assert.equal(result.searchParams.get('q'), query);
  assert.equal(result.hash, ''); assert.equal(result.searchParams.size, 1);
  assert.equal(siteSearchUrl({ ...site, url: 'https://example.test/find/%s' }, 'a/b'), 'https://example.test/find/a%2Fb');
  for (const value of ['', ' ', 'x\n', '\ud800', 'x'.repeat(4097), '😺'.repeat(1000)]) assert.throws(() => siteSearchUrl(site, value));
});

test('bounded configuration rejects unsafe schemes, authority substitution and duplicate shortcuts', () => {
  assert.ok(validSiteSearches(defaultSiteSearches)); assert.ok(validSiteSearches([]));
  for (const url of ['javascript:alert(%s)', 'sol://ui/%s', 'file:///C:/%s', 'https://%s.example/',
    'https://example.test:%s/', 'https://user:pass@example.test/?q=%s', 'https://example.test/?q=%s&x=%s',
    'https://example.test/no-token', 'https://example.test/?q=%s\n', 'https://example.test/\\?q=%s',
    'https://example.test/space here?q=%s', 'https://example.test/?q=%s&bad=%zz', 'https://a..test/?q=%s'])
    assert.equal(validSiteSearch({ ...site, url }), false, url);
  for (const keyword of ['', 'two words', 'YT', '-bad', 'x'.repeat(65)]) assert.equal(validSiteSearch({ ...site, keyword }), false);
  for (const keyword of ['blog.example.com','@youtube','example-com']) assert.equal(validSiteSearch({ ...site, keyword }),true);
  assert.ok(validSiteSearch({ ...site, enabled:false }));
  assert.equal(validSiteSearch({ ...site, enabled:0 }),false);
  assert.equal(validSiteSearch({ ...site, extra: true }), false);
  assert.equal(validSiteSearch({ ...site, name: 'x'.repeat(81) }), false);
  for (const name of ['', '   ', '\u00a0', '\ufeff']) assert.equal(validSiteSearch({ ...site, name }), false);
  assert.equal(validSiteSearches([site, site]), false);
  assert.equal(validSiteSearches(Array.from({ length: 25 }, (_, i) => ({ ...site, keyword: `s${i}` }))), false);
});

test('native and renderer use the same starter templates', () => {
  const header = readFileSync(new URL('../apps/desktop/src/navigation/site_search.h', import.meta.url), 'utf8');
  const entries = [...header.matchAll(/\{"([^"\n]+)", "([^"\n]+)", "([^"\n]+)"\}/g)].map(([,name,keyword,url]) => ({ name, keyword, url }));
  assert.deepEqual(entries, defaultSiteSearches);
});

test('snapshot contracts validate optional site-search lists and preserve older settings', () => {
  const base = { revision: 1, canGoBack: false, canGoForward: false, probeMode: false, workspaceEnabled: true,
    schemaVersion: 2, treeRevision: 1, ready: true, error: '', activeId: '', url: '', isLoading: false, nodes: [],
    sidebarVisible: true, contentBounds: { x: 0, y: 0, width: 800, height: 600 } };
  assert.deepEqual(parseSnapshot(JSON.stringify({ ...base, siteSearches: [site] })).siteSearches, [site]);
  assert.throws(() => parseSnapshot(JSON.stringify({ ...base, siteSearches: [site, site] })));
  assert.equal(parseSnapshot(JSON.stringify({...base,siteSearchActivation:'tab'})).siteSearchActivation,'tab');
  assert.throws(()=>parseSnapshot(JSON.stringify({...base,siteSearchActivation:'enter'})));
  assert.throws(()=>parseSnapshot(JSON.stringify({...base,siteSearchActivation:['tab']})));
  const settings = { ready: true, spaceName: 'Personal', spaceId: '11111111-1111-4111-8111-111111111111', version: '0.9.1', chromiumVersion: '156.0.8078.48',
    theme: { hue: 32, intensity: 70, mode: 'dark', noise: 0, dark: true }, defaultBrowser: { installed: true, registered: true, isDefault: null },
    preferencesError: '', keyboard: [], preferences: { schema: 1, searchEngine: 'google', downloadDirectory: '', askDownload: false, startup: 'restore', homepage: 'https://example.test/', shortcuts: {} } };
  assert.equal(parseSettingsSnapshot(JSON.stringify(settings)).preferences.siteSearches, undefined);
  settings.preferences.siteSearches = [];
  settings.preferences.siteSearchActivation='spaceOrTab';
  assert.deepEqual(parseSettingsSnapshot(JSON.stringify(settings)).preferences.siteSearches, []);
  assert.equal(parseSettingsSnapshot(JSON.stringify(settings)).preferences.siteSearchActivation,'spaceOrTab');
  settings.preferences.siteSearchActivation='enter';
  assert.throws(()=>parseSettingsSnapshot(JSON.stringify(settings)));
  settings.preferences.siteSearchActivation='tab';
  settings.preferences.siteSearches = [site, site];
  assert.throws(() => parseSettingsSnapshot(JSON.stringify(settings)));
});
