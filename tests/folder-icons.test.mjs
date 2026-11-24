import test from 'node:test';
import assert from 'node:assert/strict';
import { FOLDER_ICONS, findFolderIcons, folderIconEntry } from '../apps/browser-ui/src/folder-icon-model.ts';
import { FOLDER_ICON_PATHS } from '../apps/browser-ui/src/folder-icon-paths.ts';
import { validFolderIcon } from '../packages/browser-contracts/src/index.ts';

test('every catalog id is unique, storable and has exactly one glyph', () => {
  const ids = FOLDER_ICONS.map(entry => entry.id);
  assert.equal(new Set(ids).size, ids.length);
  assert.ok(ids.length >= 60);
  for (const entry of FOLDER_ICONS) {
    assert.ok(validFolderIcon(entry.id) && entry.id !== '', entry.id);
    assert.ok(entry.name && entry.keywords, entry.id);
  }
  assert.deepEqual(Object.keys(FOLDER_ICON_PATHS).sort(), [...ids].sort());
});

test('every glyph is filled path data in a 16 or 20 px box', () => {
  for (const [id, glyph] of Object.entries(FOLDER_ICON_PATHS)) {
    for (const [box, paths] of [glyph.small, glyph.large]) {
      assert.ok(box === 16 || box === 20, id);
      assert.ok(paths.length > 0 && paths.every(d => /^[0-9MmLlHhVvCcSsQqTtAaZz .,-]+$/u.test(d)), id);
    }
    assert.equal(glyph.large[0], 20, id);
  }
});

test('folder icon search takes English, Russian and Ukrainian words', () => {
  assert.equal(findFolderIcons('').length, FOLDER_ICONS.length);
  assert.equal(findFolderIcons('   ').length, FOLDER_ICONS.length);
  for (const [query, id] of [['music', 'music'], ['музыка', 'music'], ['музика', 'music'], ['Работа', 'briefcase'],
    ['ёлка', undefined], ['учёба', 'graduation-cap'], ['учеба', 'graduation-cap'], ['gamepad', 'gamepad-2'], ['code dev', 'code']]) {
    const found = findFolderIcons(query).map(entry => entry.id);
    if (id) assert.ok(found.includes(id), `${query} → ${id}`);
    else assert.deepEqual(found, [], query);
  }
  assert.deepEqual(findFolderIcons('zzz-no-match'), []);
});

test('an id from a newer build is not drawn', () => {
  assert.equal(folderIconEntry('rocket')?.name, 'Launch');
  assert.equal(folderIconEntry('future-icon'), undefined);
  assert.equal(folderIconEntry(''), undefined);
  assert.equal(folderIconEntry(undefined), undefined);
});
