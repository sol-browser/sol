import test from 'node:test';
import assert from 'node:assert/strict';
import { parseExtensionInstall } from '../apps/browser-ui/src/extension-install.ts';

const prompt = (patch = {}) => ({ name: 'uBlock Origin', title: 'Add "uBlock Origin"?', heading: 'It can:',
  accept: 'Add extension', cancel: 'Cancel', delay: 500,
  permissions: [{ text: 'Read and change all your data on all websites', details: '' }], ...patch });

test('accepts Chrome install prompt texts', () => {
  const parsed = parseExtensionInstall(prompt());
  assert.equal(parsed.name, 'uBlock Origin');
  assert.equal(parsed.accept, 'Add extension');
  assert.equal(parsed.permissions.length, 1);
  assert.equal(parsed.delay, 500);
  assert.equal(parsed.icon, undefined);
});

test('rejects prompts without a name or buttons', () => {
  assert.throws(() => parseExtensionInstall(prompt({ name: '' })));
  assert.throws(() => parseExtensionInstall(prompt({ accept: 7 })));
  assert.throws(() => parseExtensionInstall(null));
});

test('keeps only PNG data icons and bounds the delay and texts', () => {
  assert.equal(parseExtensionInstall(prompt({ icon: 'data:image/png;base64,iVBORw0KGgo=' })).icon, 'data:image/png;base64,iVBORw0KGgo=');
  assert.equal(parseExtensionInstall(prompt({ icon: 'data:image/svg+xml;base64,PHN2Zz4=' })).icon, undefined);
  assert.equal(parseExtensionInstall(prompt({ icon: 'https://example.com/a.png' })).icon, undefined);
  assert.equal(parseExtensionInstall(prompt({ delay: 60000 })).delay, 500);
  assert.equal(parseExtensionInstall(prompt({ delay: 0 })).delay, 0);
  assert.equal(parseExtensionInstall(prompt({ name: 'x'.repeat(500) })).name.length, 120);
  const many = Array.from({ length: 100 }, (_, i) => ({ text: `p${i}` }));
  assert.equal(parseExtensionInstall(prompt({ permissions: [...many, 5, null] })).permissions.length, 40);
});
