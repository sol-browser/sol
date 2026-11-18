import test from 'node:test';
import assert from 'node:assert/strict';
import { DEFAULT_THEME, MAX_SHORTCUTS, parseSettingsSnapshot } from '../packages/browser-contracts/src/index.ts';

const row = i => ({ action: `action${i}`, custom: false, defaultLabel: 'Ctrl + L', editable: true, label: 'Ctrl + L', title: `Action ${i}` });
const settings = rows => ({ theme: DEFAULT_THEME, ready: true, spaceName: 'Personal', spaceId: '1'.repeat(36), version: '0.4.1',
  chromiumVersion: '156.0.8078.88', defaultBrowser: { installed: true, registered: true, isDefault: null }, preferencesError: '',
  keyboard: Array.from({ length: rows }, (_, i) => row(i)),
  preferences: { schema: 1, searchEngine: 'google', downloadDirectory: '', askDownload: false, startup: 'restore', homepage: 'https://example.com/',
    shortcuts: {} } });

// Sol 0.4.1 has 42 shortcuts; with a limit of 40 the Settings window never came up (its data was rejected).
test('settings accept every shortcut Sol has, up to the shared limit', () => {
  for (const rows of [42, MAX_SHORTCUTS]) assert.equal(parseSettingsSnapshot(JSON.stringify(settings(rows))).keyboard.length, rows);
  assert.throws(() => parseSettingsSnapshot(JSON.stringify(settings(MAX_SHORTCUTS + 1))));
});

test('the Little Sol preferences are booleans', () => {
  for (const key of ['littleSolLinks', 'littleSolClick', 'littleSolHotkey']) {
    const value = settings(1);
    for (const flag of [true, false]) {
      value.preferences[key] = flag;
      assert.equal(parseSettingsSnapshot(JSON.stringify(value)).preferences[key], flag);
    }
    value.preferences[key] = 'yes';
    assert.throws(() => parseSettingsSnapshot(JSON.stringify(value)), /Little Sol preference/);
  }
});

test('Air Traffic Control accepts ordered rules and rejects invalid settings', () => {
  const value = settings(0);
  const read = () => parseSettingsSnapshot(JSON.stringify(value)).preferences.linkRouting;
  assert.equal(read(), undefined);
  const space = 'abcdef01-2345-6789-abcd-ef0123456789';
  const contains = { match: 'contains', pattern: 'example.com/', destination: 'recent' };
  const exact = { match: 'exact', pattern: 'https://example.com/path?x=1', destination: space };
  const regex = { match: 'regex', pattern: String.raw`example\.com/\d+$`, destination: space };
  value.preferences.linkRouting = { fallback: 'littleSol', rules: [contains, exact, regex] };
  assert.deepEqual(read(), value.preferences.linkRouting);
  for (const rule of [{ ...contains, pattern: '' }, { ...contains, pattern: 'é'.repeat(513) },
    { ...contains, destination: '../default' }, { ...exact, pattern: 'example.com' },
    { ...exact, pattern: 'https://user:pass@example.com/' }, { ...exact, extra: true }]) {
    value.preferences.linkRouting.rules = [rule];
    assert.throws(read, /Invalid link routing/);
  }
});

test('the Little Sol global shortcut round-trips and validates its keys', () => {
  const value = settings(0);
  assert.equal(parseSettingsSnapshot(JSON.stringify(value)).preferences.littleSolBinding, undefined);
  const binding = { key: 132, ctrl: true, alt: true, shift: false };
  value.preferences.littleSolBinding = binding;
  assert.deepEqual(parseSettingsSnapshot(JSON.stringify(value)).preferences.littleSolBinding, binding);
  for (const invalid of [{ key: 123 }, { key: 256 }, { alt: false }, { ctrl: 'true' }]) {
    value.preferences.littleSolBinding = { ...binding, ...invalid };
    assert.throws(() => parseSettingsSnapshot(JSON.stringify(value)), /Invalid Little Sol shortcut/);
  }
});

test('interface language is optional for old snapshots and only accepts supported settings', () => {
  const value = settings(1);
  assert.equal(parseSettingsSnapshot(JSON.stringify(value)).preferences.interfaceLanguage, undefined);
  for (const language of ['system', 'en', 'uk']) {
    value.preferences.interfaceLanguage = language;
    assert.equal(parseSettingsSnapshot(JSON.stringify(value)).preferences.interfaceLanguage, language);
  }
  for (const invalid of ['uk-UA', 'de', {}, null, 1]) {
    value.preferences.interfaceLanguage = invalid;
    assert.throws(() => parseSettingsSnapshot(JSON.stringify(value)), /Invalid interface language/);
  }
});

test('page translation settings validate the target, offer toggle and excluded sites', () => {
  const value = settings(1);
  value.translationLanguages = ['en', 'uk', 'zh_hant'];
  value.translationSourceLanguages = ['en', 'de', 'fr', 'uk', 'zh_hant'];
  Object.assign(value.preferences, { translationTarget: 'uk', translationOffers: false, translationExcludedSites: ['https://example.com/'],
    translationKnownLanguages: ['de', 'fr', 'uk'] });
  const parsed = parseSettingsSnapshot(JSON.stringify(value));
  assert.deepEqual(parsed.translationLanguages, value.translationLanguages);
  assert.equal(parsed.preferences.translationTarget, 'uk');
  assert.equal(parsed.preferences.translationOffers, false);
  assert.deepEqual(parsed.preferences.translationExcludedSites, ['https://example.com/']);
  assert.deepEqual(parsed.translationSourceLanguages, value.translationSourceLanguages);
  assert.deepEqual(parsed.preferences.translationKnownLanguages, ['de', 'fr', 'uk']);
  for (const changed of [{ translationTarget: './uk' }, { translationOffers: 'yes' }, { translationExcludedSites: [17] },
    { translationKnownLanguages: ['de', 'de'] }, { translationKnownLanguages: ['DE'] }, { translationKnownLanguages: 'de' }]) {
    assert.throws(() => parseSettingsSnapshot(JSON.stringify({ ...value, preferences: { ...value.preferences, ...changed } })), /Invalid translation preferences/);
  }
});
