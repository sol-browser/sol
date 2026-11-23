import test from 'node:test';
import assert from 'node:assert/strict';
import { parseProtectedMediaOffer, protectedMediaStatus } from '../apps/browser-ui/src/protected-media-model.ts';
import en from '../apps/browser-ui/src/locales/en.json' with { type: 'json' };
import uk from '../apps/browser-ui/src/locales/uk.json' with { type: 'json' };

// What native ProtectedMediaInstaller::Status() sends (CefWriteJSON writes the byte counts as doubles).
const status = extra => JSON.parse(JSON.stringify({ state: 'absent', percent: 0, error: '', outdated: false, manual: false,
  available: true, download: 158284551.0, installed: 374514401.0, ...extra }));

test('Protected content status is accepted as the installer sends it', () => {
  for (const state of ['absent', 'downloading', 'installing', 'installed', 'removing', 'failed'])
    assert.equal(protectedMediaStatus(status({ state })).state, state);
  const failed = protectedMediaStatus(status({ state: 'failed', error: 'integrity' }));
  assert.deepEqual([failed.error, failed.download, failed.installed], ['integrity', 158284551, 374514401]);
});

test('Protected content status rejects anything else', () => {
  for (const extra of [{ state: 'ready' }, { percent: 101 }, { percent: 1.5 }, { percent: -1 }, { error: 7 }, { error: 'x'.repeat(41) },
    { outdated: 'no' }, { manual: 1 }, { available: null }, { download: -1 }, { installed: Number.POSITIVE_INFINITY }, { download: 1e12 }])
    assert.throws(() => protectedMediaStatus({ ...status({}), ...extra }), /Invalid/, JSON.stringify(extra));
  for (const value of [null, 'installed', 3, []]) assert.throws(() => protectedMediaStatus(value), /Invalid/);
});

test('every native error code and state has a message in both languages', () => {
  for (const key of ['network', 'integrity', 'disk', 'unpack', 'inUse', 'busy', 'unavailable'])
    for (const catalog of [en, uk]) assert.ok(catalog[`settings.protected.error.${key}`], key);
  for (const key of ['installing', 'removing', 'install', 'retry', 'disk', 'downloading', 'removeDetail'])
    for (const catalog of [en, uk]) assert.ok(catalog[`settings.protected.${key}`], key);
  // The explanation must say that passwords and extensions are missing there.
  assert.match(en['settings.protected.differsDetail'].defaultMessage, /passwords.*extensions/);
});

test('the window modal offer names the service and carries the installer status', () => {
  const offer = parseProtectedMediaOffer({ service: 'Spotify', status: status({ state: 'downloading', percent: 40 }) });
  assert.deepEqual([offer.service, offer.status.state, offer.status.percent], ['Spotify', 'downloading', 40]);
  for (const value of [null, { service: '', status: status({}) }, { service: 'x'.repeat(41), status: status({}) },
    { service: 'Netflix' }, { service: 7, status: status({}) }])
    assert.throws(() => parseProtectedMediaOffer(value), /Invalid/);
  for (const key of ['title', 'detail', 'differs', 'ready', 'readyDetail', 'open', 'more', 'later', 'background'])
    for (const catalog of [en, uk]) assert.ok(catalog[`panel.protected.${key}`], key);
});

// The sidebar rejected a whole snapshot for a service it did not know (2026-11-23):
// native kServices and the UI contract must list the same services.
test('native protected services match the UI contract', async () => {
  const { readFile } = await import('node:fs/promises');
  const { protectedMediaServices } = await import('../packages/browser-contracts/src/index.ts');
  const source = await readFile(new URL('../apps/desktop/src/integrations/protected_media_host.cpp', import.meta.url), 'utf8');
  const table = source.slice(source.indexOf('kServices[] = {'), source.indexOf('};', source.indexOf('kServices[] = {')));
  const ids = [...table.matchAll(/^\s+\{"([a-z0-9]+)", "/gm)].map(match => match[1]);
  assert.deepEqual(ids, [...protectedMediaServices]);
  for (const id of ids) assert.match(id, /^[a-z0-9]{1,24}$/, 'the helper accepts only short lowercase ids');
});

test('each protected service has a sidebar color', async () => {
  const { readFile } = await import('node:fs/promises');
  const { protectedMediaServices } = await import('../packages/browser-contracts/src/index.ts');
  const css = await readFile(new URL('../apps/browser-ui/src/style.css', import.meta.url), 'utf8');
  const servicesWithColor = [...css.matchAll(/\[data-media-service="([a-z0-9]+)"\]\s*\{\s*--protected-media-color:/g)]
    .map(match => match[1]);
  assert.deepEqual(servicesWithColor.sort(), [...protectedMediaServices].sort());
});
