import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
import { createInterfaceIntl, message } from '../apps/browser-ui/src/i18n.ts';
import { commandRows } from '../apps/browser-ui/src/command-model.ts';
import { parsePreview, parseReport, previewLines, reportLines } from '../apps/browser-ui/src/import-model.ts';
import en from '../apps/browser-ui/src/locales/en.json' with { type: 'json' };
import uk from '../apps/browser-ui/src/locales/uk.json' with { type: 'json' };

const require = createRequire(new URL('../apps/browser-ui/package.json', import.meta.url));
const { parse, TYPE } = await import(pathToFileURL(require.resolve('@formatjs/icu-messageformat-parser')).href);

function placeholders(ast, result = new Set()) {
  for (const element of ast) {
    if (element.type !== TYPE.literal && element.type !== TYPE.pound) result.add(`${element.type}:${element.value}`);
    if (element.children) placeholders(element.children, result);
    if (element.options) {
      for (const option of Object.values(element.options)) placeholders(option.value, result);
    }
  }
  return result;
}

test('catalogs have valid ICU syntax and preserve the source arguments', () => {
  for (const [id, entry] of Object.entries(en)) {
    assert.ok(entry.defaultMessage.trim(), id);
    parse(entry.defaultMessage);
  }
  for (const [id, entry] of Object.entries(uk)) {
    assert.ok(Object.hasOwn(en, id), `Unknown message ${id}`);
    assert.ok(entry.defaultMessage.trim(), id);
    assert.deepEqual(placeholders(parse(entry.defaultMessage)), placeholders(parse(en[id].defaultMessage)), id);
  }
  for (const catalog of [en, uk]) {
    for (const [id, entry] of Object.entries(catalog)) {
      if (id !== 'settings.title' && !id.startsWith('native.')) continue;
      const text = entry.defaultMessage;
      assert.ok(text.length <= 200 && !/[\r\n]/u.test(text), id);
      assert.doesNotMatch(text.replaceAll('{name}', ''), /[{}]/u, id);
      if (id === 'native.window.ExitFullscreen') assert.equal(text.split('{name}').length, 2, id);
      assert.ok(parse(text).every(element => element.type === TYPE.literal ||
        (element.type === TYPE.argument && element.value === 'name')), id);
    }
  }
});

test('command palette handles Ukrainian titles and retains English searches', () => {
  const intl = createInterfaceIntl('uk');
  const translate = (id, values) => message(intl, id, values);
  for (const query of ['Налаштування', 'Settings', 'preferences']) {
    const [first] = commandRows({}, query, 'new', '', [], [], { actionsOnly: true, translate });
    assert.equal(first.action.action, 'settings');
    assert.equal(first.title, 'Налаштування');
  }
});

test('Ukrainian plurals and English fallback', () => {
  const intl = createInterfaceIntl('uk');
  for (const [count, word] of [[1, 'вкладку'], [2, 'вкладки'], [5, 'вкладок'], [21, 'вкладку']]) {
    assert.equal(message(intl, 'sidebar.clear.confirm', { count }), `Закрити ${count} ${word}?`);
  }
  assert.equal(message(createInterfaceIntl('fr'), 'settings.title'), 'Settings');
  delete intl.messages['sidebar.clear.confirm'];
  assert.equal(message(intl, 'sidebar.clear.confirm', { count: 21 }), 'Close 21 tabs?');
});

test('import preview and report use the interface language for counts', () => {
  const intl = createInterfaceIntl('uk');
  const translate = (id, values) => message(intl, id, values);
  const data = '{"label":"Arc","tabs":21,"spaces":3,"bookmarks":5}';
  assert.deepEqual(previewLines(parsePreview(data), translate), ['3 простори', '21 вкладка', '5 закладок']);
  assert.deepEqual(reportLines(parseReport(data), translate), ['Додано 3 простори', 'Додано 21 вкладку', 'Додано 5 закладок']);
});
