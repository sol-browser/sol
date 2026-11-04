import test from 'node:test';
import assert from 'node:assert/strict';
import { formatBytes, statusLine, extensionLabel, fraction, overallProgress } from '../apps/browser-ui/src/downloads-model.ts';

const item = (patch = {}) => ({ id: 1, name: 'report.pdf', origin: 'example.com', state: 'in_progress', received: 0, total: -1, speed: 0, ...patch });

test('formats byte counts compactly', () => {
  assert.equal(formatBytes(0), '0 B');
  assert.equal(formatBytes(900), '900 B');
  assert.equal(formatBytes(1536), '1.5 KB');
  assert.equal(formatBytes(12 * 1024 * 1024), '12 MB');
  assert.equal(formatBytes(4.25 * 1024 ** 3), '4.3 GB');
  assert.equal(formatBytes(-1), '');
});
test('describes each download state', () => {
  assert.equal(statusLine(item({ received: 4.2 * 1024 ** 2, total: 12 * 1024 ** 2, speed: 2 * 1024 ** 2 })), '4.2 MB / 12 MB · 2.0 MB/s · 4 s');
  assert.equal(statusLine(item({ received: 3 * 1024 ** 2, speed: 0 })), '3.0 MB');
  assert.equal(statusLine(item({ state: 'paused', received: 1024 ** 2, total: 4 * 1024 ** 2 })), 'Paused · 1.0 MB / 4.0 MB');
  assert.equal(statusLine(item({ state: 'complete', received: 12 * 1024 ** 2, total: 12 * 1024 ** 2 })), '12 MB · example.com');
  assert.equal(statusLine(item({ state: 'canceled' })), 'Canceled');
  assert.match(statusLine(item({ state: 'interrupted' })), /^Failed/);
  assert.equal(statusLine(item({ received: 10, total: 100 * 1024 ** 2, speed: 1024 ** 2 })), '10 B / 100 MB · 1.0 MB/s · 2 min');
});
test('labels files by a short extension only', () => {
  assert.equal(extensionLabel('report.pdf'), 'PDF');
  assert.equal(extensionLabel('archive.tar.gz'), 'GZ');
  assert.equal(extensionLabel('README'), 'FILE');
  assert.equal(extensionLabel('.hidden'), 'FILE');
  assert.equal(extensionLabel('weird.extension-name'), 'FILE');
});
test('computes per-item and overall progress', () => {
  assert.equal(fraction(item({ received: 50, total: 200 })), 0.25);
  assert.equal(fraction(item({ received: 50 })), undefined);
  assert.equal(fraction(item({ state: 'complete', total: -1 })), 1);
  const overall = overallProgress([item({ received: 50, total: 100 }), item({ id: 2, received: 150, total: 300 }),
    item({ id: 3, received: 10 }), item({ id: 4, state: 'complete', received: 9, total: 9 })]);
  assert.equal(overall.active, 3);
  assert.equal(overall.fraction, 0.5);
});
