import test from 'node:test';
import assert from 'node:assert/strict';
import { parseSiteInfo, isCustomized, siteHost, isSecure, PERMISSIONS } from '../apps/browser-ui/src/site-model.ts';
import { parseBoostState, isEmpty, NEUTRAL_LOOK, dotPosition, fromPadPoint, moveDot } from '../apps/browser-ui/src/boost-look.ts';

const site = (patch = {}) => ({ origin: 'https://www.example.com/', media: { camera: false, microphone: false },
  permissions: [{ key: 'camera', value: 'ask' }, { key: 'location', value: 'block' }, { key: 'popups', value: 'block' }], ...patch });

test('accepts native site settings and labels the host', () => {
  const info = parseSiteInfo(site());
  assert.equal(info.permissions.length, 3);
  assert.equal(siteHost(info.origin), 'example.com');
  assert.equal(isSecure(info.origin), true);
  assert.equal(isSecure('http://localhost:8080/'), false);
});

test('a browser page has no permissions', () => {
  assert.deepEqual(parseSiteInfo(site({ origin: '', permissions: [] })).permissions, []);
  assert.throws(() => parseSiteInfo(site({ origin: '' })), /without a site/);
});

test('rejects unknown keys, duplicates, impossible values and odd origins', () => {
  for (const [name, patch] of [
    ['unknown key', { permissions: [{ key: 'usb', value: 'allow' }] }],
    ['duplicate key', { permissions: [{ key: 'camera', value: 'ask' }, { key: 'camera', value: 'allow' }] }],
    ['ask for pop-ups', { permissions: [{ key: 'popups', value: 'ask' }] }],
    ['unknown value', { permissions: [{ key: 'camera', value: 'maybe' }] }],
    ['path in origin', { origin: 'https://example.com/path' }],
    ['script origin', { origin: 'javascript:alert(1)' }],
    ['media not boolean', { media: { camera: 'yes', microphone: false } }],
  ]) assert.throws(() => parseSiteInfo(site(patch)), undefined, name);
});

test('customized means different from Chrome defaults (ask; pop-ups blocked)', () => {
  assert.equal(isCustomized(parseSiteInfo(site({ permissions: [{ key: 'camera', value: 'ask' }, { key: 'popups', value: 'block' }] }))), false);
  assert.equal(isCustomized(parseSiteInfo(site())), true);
  assert.equal(isCustomized(parseSiteInfo(site({ permissions: [{ key: 'popups', value: 'allow' }] }))), true);
  assert.deepEqual(PERMISSIONS.popups.values, ['allow', 'block']);
});

test('extensions from the native catalog are validated', async () => {
  const { parseExtensions } = await import('../apps/browser-ui/src/site-model.ts');
  const row = (patch = {}) => ({ id: 'eimadpbcbfnmbkopoojfekhnkhdbieeh', name: 'Dark Reader', icon: 'data:image/png;base64,iVBORw0KGgo=', popup: true, options: false, ...patch });
  assert.equal(parseExtensions([row()])[0].name, 'Dark Reader');
  assert.equal(parseExtensions([row({ icon: '' })])[0].icon, '');
  assert.throws(() => parseExtensions([row({ id: 'not-an-id' })]), /Invalid extension/);
  assert.throws(() => parseExtensions([row(), row()]), /Invalid extension/);
  assert.throws(() => parseExtensions([row({ icon: 'https://evil.example/x.png' })]), /icon/);
  assert.throws(() => parseExtensions([row({ icon: 'data:image/svg+xml;base64,PHN2Zz4=' })]), /icon/);
  assert.throws(() => parseExtensions([row({ icon: 'data:image/png;base64,"><script>' })]), /icon/);
  assert.throws(() => parseExtensions([row({ name: '' })]), /Invalid extension/);
  assert.throws(() => parseExtensions({}), /Invalid extensions/);
});

test('accepts a Boost and rejects a malformed one', () => {
  const info = parseSiteInfo(site({ boost: { host: 'example.com', enabled: true, active: true } }));
  assert.deepEqual(info.boost, { host: 'example.com', enabled: true, active: true });
  assert.equal(parseSiteInfo(site()).boost, undefined);
  for (const boost of [{ host: 'Exa mple', enabled: true, active: false }, { host: 'example.com', enabled: 'yes', active: false },
    { host: 'example.com', enabled: true }, 'h1{}'])
    assert.throws(() => parseSiteInfo(site({ boost })), /Invalid Boost/);
});

test('parses the Boost editor state', () => {
  const look = { hue: 200, tint: 40, hue2: 330, tint2: 60, brightness: -10, contrast: 0, saturation: 5, dark: 1, font: 7, size: 10, caps: 1 };
  const boost = { enabled: true, css: 'h1{color:red}', look, zaps: ['#ad'], focus: '' };
  const state = parseBoostState(JSON.stringify({ host: 'example.com', page: true, picking: 'zap', boost }));
  assert.deepEqual(state, { host: 'example.com', page: true, picking: 'zap', boost });
  assert.equal(isEmpty({ enabled: true, css: '', look: { ...NEUTRAL_LOOK, hue: 120 }, zaps: [], focus: '' }), true);
  for (const bad of [{ ...look, hue: 360 }, { ...look, font: 15 }, { ...look, tint: 1.5 }, { ...look, dark: 2 }, undefined])
    assert.throws(() => parseBoostState(JSON.stringify({ host: 'example.com', page: true, picking: '', boost: { ...boost, look: bad } })), /Invalid Boost/);
  assert.throws(() => parseBoostState(JSON.stringify({ host: 'example.com', page: true, picking: 'other', boost })), /Invalid Boost/);
  assert.throws(() => parseBoostState(JSON.stringify({ host: 'example.com', page: true, picking: '', boost: { ...boost, zaps: [1] } })), /Invalid Boost/);
  assert.deepEqual(moveDot(NEUTRAL_LOOK, 'tint', { hue: 100, tint: 50 }), { hue: 100, tint: 50, hue2: 280, tint2: 35 });
  assert.deepEqual(moveDot(look, 'tint', { hue: 300, tint: 40 }), { hue: 300, tint: 40, hue2: 120, tint2: 60 });
  assert.deepEqual(moveDot(look, 'accent', { hue: 10, tint: 20 }), { hue: 190, hue2: 10, tint2: 20 });
  const p = dotPosition({ ...look, hue: 90, tint: 100 });
  assert.deepEqual(fromPadPoint(p.x, p.y), { hue: 90, tint: 100 });
});
