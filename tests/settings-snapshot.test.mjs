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
